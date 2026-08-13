// Test WS client for bridge.js — raw net socket, RFC 6455 handshake + frame parse.
// Connects, verifies replay of latest state, sends a ping, waits for a new POST push.
const net = require('net');
const crypto = require('crypto');

const key = crypto.randomBytes(16).toString('base64');
const socket = net.connect(8787, '127.0.0.1', () => {
    socket.write(
        'GET /pet HTTP/1.1\r\n' +
        'Host: 127.0.0.1:8787\r\n' +
        'Upgrade: websocket\r\n' +
        'Connection: Upgrade\r\n' +
        `Sec-WebSocket-Key: ${key}\r\n` +
        'Sec-WebSocket-Version: 13\r\n\r\n',
    );
});

let buf = Buffer.alloc(0);
let handshakeDone = false;
let pingSent = false;
let gotReplay = false;
let gotPush = false;

socket.on('data', (chunk) => {
    buf = Buffer.concat([buf, chunk]);

    // Consume HTTP 101 response first
    if (!handshakeDone) {
        const idx = buf.indexOf(Buffer.from('\r\n\r\n'));
        if (idx < 0) return; // header incomplete
        if (!buf.subarray(0, idx).includes(Buffer.from('101'))) {
            console.error('FAIL: upgrade rejected');
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
            const msg = payload;
            if (msg.includes('"state":"thinking"') && !gotReplay) {
                console.log('PASS: received pushed state (thinking)');
                gotReplay = true;
            } else if (msg.includes('"state":"working"') && !gotPush) {
                console.log('PASS: received pushed state (working)');
                gotPush = true;
                socket.end();
                process.exit(0);
            }
        } else if (opcode === 0xa) {
            console.log('PASS: got pong for ping');
        }
    }
});

socket.on('error', (e) => { console.error('FAIL: socket error', e.message); process.exit(1); });

setTimeout(() => {
    if (!gotPush) { console.error('FAIL: no pushed state within 5s (gotReplay=', gotReplay, ')'); process.exit(1); }
}, 5000);
