// Permission lifecycle test (ticket 08) — plays the ESP32 board with a raw
// net socket against its own bridge on an isolated port. Covers:
//   1. POST /permission blocks → board gets permission push + display=attention
//      → board answers permission_response (masked text frame) → POST returns
//      allow + permission_resolved + display recompute.
//   2. Queue: a second POST waits until the first settles, one push at a time.
//   3. Short-timeout bridge (PET_BRIDGE_PERMISSION_TIMEOUT_MS=400) → ask.
const net = require('net');
const http = require('http');
const crypto = require('crypto');
const { spawn } = require('child_process');
const path = require('path');

function spawnBridge(port, extraEnv) {
  const bridge = spawn(process.execPath, [path.join(__dirname, '..', 'bridge.js')], {
    env: { ...process.env, PET_BRIDGE_PORT: String(port), ...extraEnv },
    stdio: 'ignore',
  });
  bridge.on('error', (e) => { console.error('FAIL: cannot spawn bridge', e.message); process.exit(1); });
  return bridge;
}

function postPermission(port, body, onRes) {
  const payload = JSON.stringify(body);
  const req = http.request({
    host: '127.0.0.1', port, path: '/permission', method: 'POST',
    headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(payload) },
  }, (res) => {
    let data = '';
    res.on('data', (c) => (data += c));
    res.on('end', () => onRes(JSON.parse(data)));
  });
  req.on('error', () => {});
  req.end(payload);
}

/* Raw WS fake board */
function connectBoard(port) {
  const socket = net.connect(port, '127.0.0.1', () => {
    socket.write(
      'GET /pet HTTP/1.1\r\n' +
      `Host: 127.0.0.1:${port}\r\n` +
      'Upgrade: websocket\r\n' +
      'Connection: Upgrade\r\n' +
      `Sec-WebSocket-Key: ${crypto.randomBytes(16).toString('base64')}\r\n` +
      'Sec-WebSocket-Version: 13\r\n\r\n',
    );
  });
  socket.on('error', () => {});
  return socket;
}

/* Parse the 101 + subsequent frames; cb(typeMsg) per text message. */
function makeFrameReader(onReady) {
  let buf = Buffer.alloc(0);
  let handshakeDone = false;
  return function onData(chunk) {
    buf = Buffer.concat([buf, chunk]);
    if (!handshakeDone) {
      const idx = buf.indexOf(Buffer.from('\r\n\r\n'));
      if (idx < 0) return;
      buf = buf.subarray(idx + 4);
      handshakeDone = true;
      onReady();
    }
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
        try { onReady(JSON.parse(payload)); } catch { /* skip non-JSON */ }
      }
    }
  };
}

/* Client→server text frame, masked (RFC 6455 requires client masking). */
function sendMaskedText(socket, obj) {
  const payload = Buffer.from(JSON.stringify(obj));
  const mask = Buffer.from([7, 6, 5, 4]);
  const masked = Buffer.from(payload);
  for (let i = 0; i < masked.length; i++) masked[i] ^= mask[i & 3];
  let header;
  if (payload.length < 126) {
    header = Buffer.from([0x81, 0x80 | payload.length]);
  } else {
    header = Buffer.alloc(4);
    header[0] = 0x81;
    header[1] = 0x80 | 126;
    header.writeUInt16BE(payload.length, 2);
  }
  socket.write(Buffer.concat([header, mask, masked]));
}

let failures = 0;
function check(name, cond, detail) {
  if (cond) {
    console.log(`PASS: ${name}`);
  } else {
    console.error(`FAIL: ${name}${detail ? ` (${detail})` : ''}`);
    failures++;
  }
}

/* ---------------- phase 1: full chain + queue ---------------- */
const P1 = 18789;
const bridge1 = spawnBridge(P1);
let board;
setTimeout(() => { board = connectBoard(P1); board.on('data', onMsg); }, 300); // let the child listen

let phase1Done = false;
function finishPhase1() {
  if (phase1Done) return;
  phase1Done = true;
  board.end();
  bridge1.kill();
  phase2();
}

/* The finish gate is a conjunction of events from two sockets (WS and HTTP)
 * with no cross-socket ordering — evaluate it on EVERY event, whichever
 * arrives last completes the phase. */
function maybeFinish() {
  if (answeredA && answeredB && resolvedA && resolvedB && pushedB && sawAttention && sawIdleAgain) {
    finishPhase1();
  }
}

let answeredA = false;
let answeredB = false;
let resolvedA = false;
let resolvedB = false;
let pushedB = false;
let sawAttention = false;
let sawIdleAgain = false;
let sentA = false;
let sentB = false;

const onMsg = makeFrameReader((msg) => {
  if (typeof msg !== 'object' || !msg) return;
  if (msg.type === 'snapshot' && !sentA) {
    // board is registered — now it's safe to POST (pushes would be lost
    // before the WS handshake completes)
    sentA = true;
    postPermission(P1, { permission_id: 'p1', tool: 'Bash', hint: 'rm -rf /tmp/x' }, (res) => {
      answeredA = true;
      check('p1 POST answered allow after board response', res.decision === 'allow', res.decision);
      maybeFinish();
    });
    return;
  }
  if (msg.type === 'permission' && msg.permission_id === 'p1') {
    check('permission push for p1 while POST blocks', !answeredA && !answeredB);
    sendPermissionResponseFor('p1', 'once');
    if (!sentB) {
      // queue: p2 arrives while p1 is still pending — it must wait its turn
      sentB = true;
      postPermission(P1, { permission_id: 'p2', tool: 'Write', hint: 'overwrite file' }, (res) => {
        answeredB = true;
        check('p2 POST answered deny after board response', res.decision === 'deny', res.decision);
        maybeFinish();
      });
    }
  }
  if (msg.type === 'permission' && msg.permission_id === 'p2') {
    pushedB = true;
    check('p2 pushed only after p1 settled', resolvedA);
    sendPermissionResponseFor('p2', 'deny');
  }
  if (msg.type === 'display' && msg.state === 'attention') {
    sawAttention = true;
    check('display forced to attention while pending', true);
  }
  if (msg.type === 'display' && msg.state === 'idle' && resolvedB) {
    // gate on resolvedB, not the POST callbacks: the idle broadcast follows
    // the resolved broadcast on the same WS socket (ordered), while answeredB
    // arrives over a separate HTTP socket — a race that flips under load
    sawIdleAgain = true;
    check('display recomputed after resolve', true);
  }
  if (msg.type === 'permission_resolved' && msg.permission_id === 'p1') {
    resolvedA = true;
    check('permission_resolved pushed for p1', true);
  }
  if (msg.type === 'permission_resolved' && msg.permission_id === 'p2') {
    resolvedB = true;
    check('permission_resolved pushed for p2', true);
  }
  maybeFinish();
});

function sendPermissionResponseFor(id, decision) {
  sendMaskedText(board, { version: 'v1', type: 'permission_response', permission_id: id, decision, timestamp: Date.now() });
}

/* ---------------- phase 2: short timeout → ask ---------------- */
function phase2() {
  const P2 = 18790;
  const bridge2 = spawnBridge(P2, { PET_BRIDGE_PERMISSION_TIMEOUT_MS: '400' });
  setTimeout(() => {
    postPermission(P2, { permission_id: 'p3', tool: 'Bash', hint: 'no board' }, (res) => {
      check('timeout falls back to ask', res.decision === 'ask', res.decision);
      bridge2.kill();
      if (failures > 0) {
        console.error(`${failures} check(s) failed`);
        process.exit(1);
      }
      console.log('all permission checks passed');
      process.exit(0);
    });
  }, 300); // let the child listen
}

setTimeout(() => {
  console.error('FAIL: incomplete within 8s (phase1Done=', phase1Done, ')');
  bridge1.kill();
  process.exit(1);
}, 8000);
