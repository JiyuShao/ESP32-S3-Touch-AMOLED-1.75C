// Test WS client for bridge.js — raw net socket, RFC 6455 handshake + frame parse.
// Spawns its own bridge on an isolated port (the real daemon on 8787 keeps
// receiving hook traffic from this very session, which would race the
// assertions). POSTs "thinking" (expect snapshot replay on connect), then
// after handshake+pong POSTs "working" and expects the live state + display
// pushes. Self-contained.
const net = require('net');
const http = require('http');
const crypto = require('crypto');
const { spawn } = require('child_process');
const path = require('path');

const PORT = 18787;
const bridge = spawn(process.execPath, [path.join(__dirname, '..', 'bridge.js')], {
    env: { ...process.env, PET_BRIDGE_PORT: String(PORT) },
    stdio: 'ignore',
});
bridge.on('error', (e) => { console.error('FAIL: cannot spawn bridge', e.message); process.exit(1); });
const killBridge = () => { try { bridge.kill(); } catch {} };

function post(state, extra, onDone) {
    const body = JSON.stringify({ state, session_id: 'test', ...extra });
    const req = http.request({
        host: '127.0.0.1', port: PORT, path: '/state', method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) },
    }, (res) => { res.resume(); res.on('end', () => onDone && onDone()); });
    req.on('error', () => {});
    req.end(body);
}

const key = crypto.randomBytes(16).toString('base64');
let socket;
function startWs() {
    socket = net.connect(PORT, '127.0.0.1', () => {
        socket.write(
            'GET /pet HTTP/1.1\r\n' +
            `Host: 127.0.0.1:${PORT}\r\n` +
            'Upgrade: websocket\r\n' +
            'Connection: Upgrade\r\n' +
            `Sec-WebSocket-Key: ${key}\r\n` +
            'Sec-WebSocket-Version: 13\r\n\r\n',
        );
    });
    socket.on('data', onData);
    socket.on('error', (e) => { console.error('FAIL: socket error', e.message); killBridge(); process.exit(1); });
}

let buf = Buffer.alloc(0);
let handshakeDone = false;
let pingSent = false;
let gotSnapshot = false;
let gotStatePush = false;
let gotDisplayPush = false;

function finishIfDone() {
    if (gotSnapshot && gotStatePush && gotDisplayPush) {
        socket.end();
        killBridge();
        process.exit(0);
    }
}

function onData(chunk) {
    buf = Buffer.concat([buf, chunk]);

    // Consume HTTP 101 response first
    if (!handshakeDone) {
        const idx = buf.indexOf(Buffer.from('\r\n\r\n'));
        if (idx < 0) return; // header incomplete
        if (!buf.subarray(0, idx).includes(Buffer.from('101'))) {
            console.error('FAIL: upgrade rejected');
            killBridge();
            process.exit(1);
        }
        buf = buf.subarray(idx + 4);
        handshakeDone = true;
    }

    // Send one masked ping right after handshake to test pong handling
    if (!pingSent && handshakeDone) {
        pingSent = true;
        const pingPayload = Buffer.from('hi');
        const mask = Buffer.from([1, 2, 3, 4]);
        const masked = Buffer.from(pingPayload);
        for (let i = 0; i < masked.length; i++) masked[i] ^= mask[i & 3];
        socket.write(Buffer.concat([Buffer.from([0x89, 0x80 | pingPayload.length]), mask, masked]));
        post('working'); // now that we're a registered client, expect the pushes
    }

    // Parse WS frames (server→client: unmasked)
    while (buf.length >= 2) {
        const opcode = buf[0] & 0x0f;
        let len = buf[1] & 0x7f;
        let off = 2;
        if (len === 126) { if (buf.length < 4) return; len = buf.readUInt16BE(2); off = 4; }
        else if (len === 127) { if (buf.length < 10) return; len = Number(buf.readBigUInt64BE(2)); off = 10; }
        if (buf.length < off + len) return;
        const payload = buf.subarray(off, off + len).toString();
        buf = buf.subarray(off + len);

        if (opcode === 0x1) {
            let msg;
            try { msg = JSON.parse(payload); } catch { continue; }
            if (msg.type === 'snapshot' && !gotSnapshot) {
                const sess = msg.sessions.find((s) => s.session_id === 'test');
                if (!sess || sess.state !== 'thinking' || sess.basename !== 'proj') {
                    console.error('FAIL: snapshot missing/bad session record', JSON.stringify(msg.sessions));
                    killBridge();
                    process.exit(1);
                }
                if (msg.display !== 'thinking') {
                    console.error('FAIL: snapshot display wrong', msg.display);
                    killBridge();
                    process.exit(1);
                }
                console.log('PASS: snapshot on connect (session list + display)');
                gotSnapshot = true;
                finishIfDone();
            } else if (msg.type === 'state' && msg.session_id === 'test' && msg.state === 'working' && !gotStatePush) {
                console.log('PASS: live state push (working)');
                gotStatePush = true;
                finishIfDone();
            } else if (msg.type === 'display' && msg.state === 'working' && !gotDisplayPush) {
                console.log('PASS: live display push (working)');
                gotDisplayPush = true;
                finishIfDone();
            }
        } else if (opcode === 0xa) {
            console.log('PASS: got pong for ping');
        }
    }
}

setTimeout(() => {
    console.error('FAIL: incomplete within 5s (snapshot=', gotSnapshot,
        'state=', gotStatePush, 'display=', gotDisplayPush, ')');
    killBridge();
    process.exit(1);
}, 5000);

// give the child bridge a moment to listen, then seed it before connecting
setTimeout(() => post('thinking', { basename: 'proj' }, startWs), 300);
