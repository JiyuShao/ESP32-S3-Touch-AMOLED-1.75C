#!/usr/bin/env node
/*
 * Pet Bridge — minimal PC-side daemon (ticket 02).
 *
 * HTTP  POST /state  → validate JSON (must have string "state"), forward to WS clients
 * WS    GET  /pet    → WebSocket upgrade; pushes JSON on every state change
 *
 * Zero npm dependencies (node built-ins only). Run: node bridge.js
 * Priority resolution (Clawd 12-state) arrives in ticket 04.
 */

const http = require('http');
const crypto = require('crypto');

const PORT = 8787;
const WS_MAGIC = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

const clients = new Set(); // connected WS sockets
let latestState = null;

/* ---------------- WebSocket framing (RFC 6455) ---------------- */

function sendFrame(socket, text) {
    const payload = Buffer.from(text);
    let header;
    if (payload.length < 126) {
        header = Buffer.from([0x81, payload.length]); // FIN + text, unmasked (server→client)
    } else if (payload.length < 65536) {
        header = Buffer.alloc(4);
        header[0] = 0x81;
        header[1] = 126;
        header.writeUInt16BE(payload.length, 2);
    } else {
        header = Buffer.alloc(10);
        header[0] = 0x81;
        header[1] = 127;
        header.writeBigUInt64BE(BigInt(payload.length), 2);
    }
    socket.write(Buffer.concat([header, payload]));
}

function sendPong(socket, payload) {
    const header = Buffer.from([0x8a, payload.length]); // FIN + pong
    socket.write(Buffer.concat([header, payload]));
}

function sendClose(socket) {
    socket.write(Buffer.from([0x88, 0x00])); // FIN + close, no payload
}

/* Frame parser for client→server (masked frames). Only handles what
 * esp_websocket_client actually sends: ping and close. */
function makeWsReceiver(socket) {
    let buf = Buffer.alloc(0);
    return function (chunk) {
        buf = Buffer.concat([buf, chunk]);
        while (buf.length >= 2) {
            const fin = (buf[0] & 0x80) !== 0;
            const opcode = buf[0] & 0x0f;
            const masked = (buf[1] & 0x80) !== 0;
            let len = buf[1] & 0x7f;
            let off = 2;
            if (len === 126) {
                if (buf.length < 4) return;
                len = buf.readUInt16BE(2);
                off = 4;
            } else if (len === 127) {
                if (buf.length < 10) return;
                len = Number(buf.readBigUInt64BE(2));
                off = 10;
            }
            const maskLen = masked ? 4 : 0;
            if (buf.length < off + maskLen + len) return; // wait for full frame
            const mask = masked ? buf.subarray(off, off + 4) : null;
            const payload = Buffer.from(buf.subarray(off + maskLen, off + maskLen + len));
            if (mask) {
                for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
            }
            buf = buf.subarray(off + maskLen + len);

            if (opcode === 0x9) { // ping → pong
                sendPong(socket, payload);
            } else if (opcode === 0x8) { // close → ack + drop
                sendClose(socket);
                socket.end();
                return;
            } else if (opcode === 0x1) {
                // ESP32 text messages (future: permission responses). Ignore for now.
            }
            if (!fin) { /* fragmented control frames are illegal; ignore */ }
        }
    };
}

/* ---------------- HTTP + WS upgrade ---------------- */

const server = http.createServer((req, res) => {
    if (req.method === 'POST' && req.url === '/state') {
        let body = '';
        req.on('data', (chunk) => {
            body += chunk;
            if (body.length > 65536) req.destroy(); // don't feed unbounded input
        });
        req.on('end', () => {
            let json;
            try {
                json = JSON.parse(body);
            } catch {
                res.writeHead(400, { 'Content-Type': 'text/plain' }).end('bad json');
                return;
            }
            if (typeof json.state !== 'string') {
                res.writeHead(400, { 'Content-Type': 'text/plain' }).end('missing state field');
                return;
            }
            latestState = json;
            console.log(`[bridge] state: ${json.state} (session: ${json.session_id || '?'})`);
            for (const s of clients) sendFrame(s, JSON.stringify(json));
            res.writeHead(200, { 'Content-Type': 'text/plain' }).end('ok');
        });
        return;
    }

    if ((req.headers.upgrade || '').toLowerCase() === 'websocket') {
        const key = req.headers['sec-websocket-key'];
        if (!key) {
            res.writeHead(400).end();
            return;
        }
        const accept = crypto.createHash('sha1').update(key + WS_MAGIC).digest('base64');
        const socket = req.socket;
        socket.write(
            'HTTP/1.1 101 Switching Protocols\r\n' +
            'Upgrade: websocket\r\n' +
            'Connection: Upgrade\r\n' +
            `Sec-WebSocket-Accept: ${accept}\r\n\r\n`,
        );
        socket.setNoDelay(true);
        clients.add(socket);
        console.log(`[bridge] ws client connected (total: ${clients.size})`);
        if (latestState) sendFrame(socket, JSON.stringify(latestState)); // replay latest on connect

        socket.on('data', makeWsReceiver(socket));
        socket.on('close', () => {
            clients.delete(socket);
            console.log(`[bridge] ws client gone (total: ${clients.size})`);
        });
        socket.on('error', () => {});
        return;
    }

    res.writeHead(404).end();
});

server.listen(PORT, () => {
    console.log(`[bridge] listening on http://0.0.0.0:${PORT}`);
    console.log('[bridge] POST /state  → ingest agent state');
    console.log('[bridge] WS   /pet    → push state to ESP32');
});
