/**
 * OCPP 1.6J Simulator for CitrineOS Testing
 * Usage: 
 *   1. npm install ws
 *   2. node ocpp_simulator.js --mode sequence
 *   3. node ocpp_simulator.js --mode listen
 */

const WebSocket = require('ws');
const { v4: uuidv4 } = require('uuid');

const CHARGER_ID = 'SIM_250822008C06';
const WS_URL = `ws://localhost:8092/${CHARGER_ID}`;

const args = process.argv.slice(2);
const mode = args.includes('--mode') ? args[args.indexOf('--mode') + 1] : 'sequence';

console.log(`[SIM] Starting OCPP Simulator (ID: ${CHARGER_ID})`);
console.log(`[SIM] Connecting to: ${WS_URL}`);

const ws = new WebSocket(WS_URL, 'ocpp1.6');
let currentTransactionId = null;

ws.on('open', () => {
    console.log('[SIM] Connected to CSMS');
    
    if (mode === 'sequence') {
        runSequence();
    } else {
        console.log('[SIM] Mode: LISTEN (waiting for CSMS commands)');
        // Send initial status just to show up on server
        sendStatus('Available');
    }
});

ws.on('message', (data) => {
    const message = JSON.parse(data);
    const [msgId, callId, action, payload] = message;

    if (msgId === 2) { // CALL
        console.log(`[SIM] 📥 Received Action: ${action}`);
        handleCall(callId, action, payload);
    } else if (msgId === 3) { // CALLRESULT
        console.log(`[SIM] ✅ Received Response for ID: ${callId}`);
        console.log(`[SIM] Payload: ${JSON.stringify(payload)}`);
    } else if (msgId === 4) { // CALLERROR
        console.error(`[SIM] ❌ Received Error for ID: ${callId}`, payload);
    }
});

ws.on('close', () => {
    console.log('[SIM] Disconnected');
});

ws.on('error', (err) => {
    console.error('[SIM] WebSocket Error:', err.message);
});

// --- OCPP Actions ---

function sendRequest(action, payload) {
    const callId = uuidv4();
    const message = JSON.stringify([2, callId, action, payload]);
    console.log(`[SIM] 📤 Sending ${action}...`);
    ws.send(message);
    return callId;
}

function sendResponse(callId, payload) {
    const message = JSON.stringify([3, callId, payload]);
    ws.send(message);
}

function handleCall(callId, action, payload) {
    switch (action) {
        case 'RemoteStartTransaction':
            console.log(`[SIM] 🚀 Remote Start requested (idTag: ${payload.idTag})`);
            sendResponse(callId, { status: 'Accepted' });
            // Simulate starting transaction after acceptance
            setTimeout(() => {
                sendStartTransaction(payload.idTag);
            }, 1000);
            break;
            
        case 'RemoteStopTransaction':
            console.log(`[SIM] 🛑 Remote Stop requested (txId: ${payload.transactionId})`);
            sendResponse(callId, { status: 'Accepted' });
            setTimeout(() => {
                sendStopTransaction(payload.transactionId);
            }, 1000);
            break;

        case 'Reset':
            console.log(`[SIM] 🔄 Reset requested: ${payload.type}`);
            sendResponse(callId, { status: 'Accepted' });
            break;

        case 'GetConfiguration':
            sendResponse(callId, { configurationKey: [{ key: 'HeartbeatInterval', readonly: false, value: '60' }] });
            break;

        case 'ChangeConfiguration':
            console.log(`[SIM] ⚙️ Config Change: ${payload.key} = ${payload.value}`);
            sendResponse(callId, { status: 'Accepted' });
            break;

        default:
            console.log(`[SIM] ⚠️ Action ${action} not implemented in simulator`);
            sendResponse(callId, { status: 'NotImplemented' });
    }
}

// --- Simulation logic ---

function sendStatus(status) {
    sendRequest('StatusNotification', {
        connectorId: 1,
        errorCode: 'NoError',
        status: status,
        timestamp: new Date().toISOString()
    });
}

function sendStartTransaction(idTag = 'TEST_TAG') {
    sendRequest('StartTransaction', {
        connectorId: 1,
        idTag: idTag,
        meterStart: 0,
        timestamp: new Date().toISOString()
    });
}

function sendStopTransaction(txId) {
    sendRequest('StopTransaction', {
        transactionId: txId || currentTransactionId,
        meterStop: 1500,
        timestamp: new Date().toISOString(),
        reason: 'Remote'
    });
}

async function runSequence() {
    console.log('[SIM] Starting Automated Sequence...');
    
    // 1. Boot
    sendRequest('BootNotification', {
        chargePointModel: 'Simulator-v1',
        chargePointVendor: 'RivotMotors'
    });

    await delay(2000);

    // 2. Status Available
    sendStatus('Available');
    await delay(1000);

    // 3. Status Preparing (Plug In)
    sendStatus('Preparing');
    await delay(2000);

    // 4. Authorize
    sendRequest('Authorize', { idTag: 'SIM_TAG_001' });
    await delay(1000);

    // 5. Start Transaction
    sendRequest('StartTransaction', {
        connectorId: 1,
        idTag: 'SIM_TAG_001',
        meterStart: 100,
        timestamp: new Date().toISOString()
    });
}

function delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}
