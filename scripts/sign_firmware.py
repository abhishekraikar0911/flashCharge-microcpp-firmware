#!/usr/bin/env python3
"""Sign firmware.bin with ECDSA P-256 and append raw r||s signature (64 bytes)."""
import argparse
import hashlib
from pathlib import Path

from typing import Optional

try:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, utils
except ImportError as exc:
    raise SystemExit("Missing dependency: pip install cryptography") from exc


def load_private_key(path: Path, password: Optional[str]):
    data = path.read_bytes()
    pwd = password.encode("utf-8") if password else None
    return serialization.load_pem_private_key(data, password=pwd)


def sign_firmware(fw_path: Path, key_path: Path, out_path: Path, password: Optional[str]):
    firmware = fw_path.read_bytes()
    digest = hashlib.sha256(firmware).digest()

    key = load_private_key(key_path, password)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("Private key must be ECDSA P-256 (secp256r1)")

    signature_der = key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(signature_der)
    sig = r.to_bytes(32, "big") + s.to_bytes(32, "big")

    if len(sig) != 64:
        raise ValueError("Signature length is not 64 bytes (unexpected)")

    out_path.write_bytes(firmware + sig)
    print(f"Signed firmware written to: {out_path}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", nargs="?", default="../.pio/build/charger_esp32_production/firmware.bin")
    parser.add_argument("key", nargs="?", help="Path to ECDSA P-256 private key (PEM)")
    parser.add_argument("out", nargs="?", default="../.pio/build/charger_esp32_production/firmware.signed.bin")
    parser.add_argument("--firmware", dest="firmware_flag", default=None)
    parser.add_argument("--key", dest="key_flag", default=None, help="Path to ECDSA P-256 private key (PEM)")
    parser.add_argument("--out", dest="out_flag", default=None)
    parser.add_argument("--password", default=None, help="PEM password (if encrypted)")
    args = parser.parse_args()

    firmware = args.firmware_flag or args.firmware
    key = args.key_flag or args.key
    out = args.out_flag or args.out

    if not key:
        raise SystemExit("Missing key. Provide positional key or --key.")

    sign_firmware(Path(firmware), Path(key), Path(out), args.password)


if __name__ == "__main__":
    main()
