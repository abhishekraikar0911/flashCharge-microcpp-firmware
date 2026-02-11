#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   PGHOST=localhost PGPORT=5432 PGDATABASE=csms PGUSER=postgres PGPASSWORD=secret \
#     ./scripts/safe_remote_start_stop.sh 250822008C06 1 TEST123
# Adjust CSMS_API if your management API is on a different host/port.

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 <STATION_ID> <CONNECTOR_ID> <IDTAG>"
  exit 2
fi

STATION="$1"
CONNECTOR="$2"
IDTAG="$3"
CSMS_API="http://localhost:8081/ocpp/1.6/evdriver"

PSQL_HOST="${PGHOST:-localhost}"
PSQL_PORT="${PGPORT:-5432}"
PSQL_DB="${PGDATABASE:-csms}"
PSQL_USER="${PGUSER:-postgres}"
# Ensure PGPASSWORD is set in env or .pgpass is configured

echo "Triggering RemoteStartTransaction for station=$STATION connector=$CONNECTOR idTag=$IDTAG"
curl -s -X POST "${CSMS_API}/remoteStartTransaction?identifier=${STATION}" \
  -H 'Content-Type: application/json' \
  -d "{\"idTag\":\"${IDTAG}\",\"connectorId\":${CONNECTOR}}" || true

echo "Polling DB for server-assigned transactionId (timeout 60s)..."
TXID=0
for i in $(seq 1 60); do
  TXID=$(psql -h "$PSQL_HOST" -p "$PSQL_PORT" -U "$PSQL_USER" -d "$PSQL_DB" -t -A -c \
    "SELECT \"transactionId\" FROM \"Transactions\" WHERE \"stationId\"='${STATION}' AND \"connectorId\"=${CONNECTOR} ORDER BY \"createdAt\" DESC LIMIT 1;" 2>/dev/null || true)
  TXID=${TXID//[[:space:]]/}
  if [[ -n "$TXID" && "$TXID" != "0" ]]; then
    echo "Found transactionId=$TXID"
    break
  fi
  sleep 1
done

if [[ -z "$TXID" || "$TXID" == "0" ]]; then
  echo "Timed out waiting for a valid transactionId. Check server logs and DB."
  exit 3
fi

echo "Issuing RemoteStopTransaction for transactionId=$TXID"
# Some CSMS endpoints accept just identifier; include transactionId as query if supported.
curl -s -X POST "${CSMS_API}/remoteStopTransaction?identifier=${STATION}&transactionId=${TXID}" -H 'Content-Type: application/json' -d "{}" || true

echo "Done. Check CSMS logs and station behavior."
