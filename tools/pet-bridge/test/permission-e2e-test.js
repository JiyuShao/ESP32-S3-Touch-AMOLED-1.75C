// End-to-end permission test (ticket 08) — runs the REAL hook process
// (hooks/claude-code-permission-hook.js) against a bridge + fake board:
//   1. fake board auto-approves → hook stdout outputs permissionDecision=allow
//   2. no board → bridge times out (500 ms env) → hook outputs ask
const net = require('net');
const http = require('http');
const crypto = require('crypto');
const { spawn } = require('child_process');
const path = require('path');

const HOOK = path.join(__dirname, '..', 'hooks', 'claude-code-permission-hook.js');
const HOOK_INPUT = JSON.stringify({
  hook_event_name: 'PermissionRequest',
  session_id: 'e2e-session',
  cwd: '/tmp/e2e-proj',
  tool_name: 'Bash',
  tool_input: { command: 'echo hi' },
});

function spawnBridge(port) {
  const bridge = spawn(process.execPath, [path.join(__dirname, '..', 'bridge.js')], {
    env: {
      ...process.env,
      PET_BRIDGE_PORT: String(port),
      PET_BRIDGE_PERMISSION_TIMEOUT_MS: '500',
    },
    stdio: 'ignore',
  });
  bridge.on('error', (e) => { console.error('FAIL: cannot spawn bridge', e.message); process.exit(1); });
  return bridge;
}

/* Client→server text frame, masked (RFC 6455 requires client masking). */
function sendMaskedText(socket, obj) {
  const payload = Buffer.from(JSON.stringify(obj));
  const mask = Buffer.from([1, 2, 3, 4]);
  for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
  let header;
  if (payload.length < 126) {
    header = Buffer.from([0x81, 0x80 | payload.length]);
  } else {
    header = Buffer.alloc(4);
    header[0] = 0x81;
    header[1] = 0x80 | 126;
    header.writeUInt16BE(payload.length, 2);
  }
  socket.write(Buffer.concat([header, mask, payload]));
}

/* Fake board that auto-answers any permission push with "once". */
function startApprovingBoard(port, done) {
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
  let buf = Buffer.alloc(0);
  let handshakeDone = false;
  socket.on('data', (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    if (!handshakeDone) {
      const idx = buf.indexOf(Buffer.from('\r\n\r\n'));
      if (idx < 0) return;
      buf = buf.subarray(idx + 4);
      handshakeDone = true;
      done(socket);
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
        let msg;
        try { msg = JSON.parse(payload); } catch { continue; }
        if (msg.type === 'permission') {
          sendMaskedText(socket, {
            version: 'v1',
            type: 'permission_response',
            permission_id: msg.permission_id,
            decision: 'once',
            timestamp: Date.now(),
          });
        }
      }
    }
  });
  return socket;
}

/* Run the real hook with the given bridge port; resolve with its decision. */
function runHook(port) {
  return new Promise((resolve, reject) => {
    const hook = spawn(process.execPath, [HOOK], {
      env: { ...process.env, PET_BRIDGE_PORT: String(port), PET_BRIDGE_PERMISSION_TIMEOUT_MS: '500' },
    });
    let out = '';
    hook.stdout.on('data', (d) => (out += d));
    hook.on('error', reject);
    hook.on('close', () => {
      try {
        const parsed = JSON.parse(out);
        resolve(parsed.hookSpecificOutput.permissionDecision);
      } catch {
        reject(new Error(`hook emitted garbage: ${out}`));
      }
    });
    hook.stdin.end(HOOK_INPUT);
  });
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

/* ---------------- phase 1: approving board → allow ---------------- */
const P1 = 18791;
const bridge1 = spawnBridge(P1);
setTimeout(() => { // let the child listen
  const board1 = startApprovingBoard(P1, () => {
    runHook(P1).then((decision) => {
      check('real hook + approving board → allow', decision === 'allow', decision);
      board1.end();
      bridge1.kill();
      phase2();
    }).catch((e) => { console.error('FAIL:', e.message); failures++; bridge1.kill(); phase2(); });
  });
}, 300);

/* ---------------- phase 2: no board → ask ---------------- */
function phase2() {
  const P2 = 18792;
  const bridge2 = spawnBridge(P2);
  setTimeout(() => { // the hook must reach a LISTENING bridge with no board
    runHook(P2).then((decision) => {
      check('real hook + absent board → ask', decision === 'ask', decision);
      bridge2.kill();
      if (failures > 0) {
        console.error(`${failures} check(s) failed`);
        process.exit(1);
      }
      console.log('all e2e permission checks passed');
      process.exit(0);
    }).catch((e) => { console.error('FAIL:', e.message); process.exit(1); });
  }, 300);
}

setTimeout(() => {
  console.error('FAIL: e2e incomplete within 10s');
  process.exit(1);
}, 10000);
