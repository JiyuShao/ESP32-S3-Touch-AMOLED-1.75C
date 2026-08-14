#!/usr/bin/env node
/*
 * Pet Bridge — PC-side daemon: agent states in, session list + dominant
 * display state out (ticket 06, Clawd-aligned wire protocol), plus the
 * board-mediated permission lifecycle (ticket 08).
 *
 * HTTP  POST /state       → ingest {"state": "<clawd-state>", "session_id", ...}
 * HTTP  POST /permission  → block until the board answers allow/deny or the
 *                           150 s timeout falls back to "ask" (fail-open)
 * WS    GET  /pet         → snapshot on connect, then incremental state /
 *                           session_deleted / display / permission /
 *                           permission_resolved pushes (Clawd envelope:
 *                           every message carries version / type / timestamp)
 *
 * Sessions are tracked per session_id; idle sessions expire after 5 min,
 * active ones after 30 min (long tool calls survive). "sleeping" (the
 * SessionEnd state) removes a session immediately. The dominant session
 * (Clawd priority table) maps to one of the pet's 6 display states.
 * While a permission request is pending the display is forced to
 * "attention"; the board's permission_response text frame settles it.
 * Zero npm dependencies. Run: node bridge.js
 */

const http = require('http');
const crypto = require('crypto');
const { isValidState, resolveDominantState, DISPLAY_STATE, getStatePriority } = require('./priority');

const PORT = Number(process.env.PET_BRIDGE_PORT) || 8787;
const WS_MAGIC = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const IDLE_TTL_MS = Number(process.env.PET_BRIDGE_SESSION_TTL_MS) || 5 * 60 * 1000;
const ACTIVE_TTL_MS = Number(process.env.PET_BRIDGE_ACTIVE_TTL_MS) || 30 * 60 * 1000;
const SWEEP_INTERVAL_MS = 60 * 1000;
const MAX_SESSIONS = 8;
const PERMISSION_TIMEOUT_MS = Number(process.env.PET_BRIDGE_PERMISSION_TIMEOUT_MS) || 150 * 1000;

const clients = new Set(); // connected WS sockets
const sessions = new Map(); // session_id → { payload, basename, updatedAt }
let lastDisplay = 'idle';
const permissionQueue = []; // { id, tool, hint, resolve } — waiting their turn
let pendingPermission = null; // the one request pushed to the board { id, tool, hint, resolve, timer }

/* ---------------- session + priority resolution ---------------- */

function ttlOf(payload) {
  return payload.state === 'idle' || payload.state === 'sleeping' ? IDLE_TTL_MS : ACTIVE_TTL_MS;
}

function sweepStale(now) {
  for (const [id, s] of sessions) {
    if (now - s.updatedAt > ttlOf(s.payload)) {
      sessions.delete(id);
      console.log(`[bridge] session expired: ${id}`);
      broadcastMsg({ version: 'v1', type: 'session_deleted', timestamp: now, session_id: id });
    }
  }
}

function dominantSession() {
  return resolveDominantState([...sessions.values()].map((s) => s.payload));
}

function currentDisplay() {
  if (pendingPermission) return 'attention'; // approval owns the display while pending
  const dom = dominantSession();
  return dom ? (DISPLAY_STATE[dom.state] ?? 'idle') : 'idle';
}

/* One wire record for a session: display-mapped state + sort key. */
function sessionRecord(id, s) {
  return {
    session_id: id,
    basename: s.basename,
    state: DISPLAY_STATE[s.payload.state] ?? 'idle',
    updated_at: s.updatedAt,
    priority: getStatePriority(s.payload.state),
  };
}

function sortedRecords() {
  return [...sessions.entries()]
    .map(([id, s]) => sessionRecord(id, s))
    .sort((a, b) => b.priority - a.priority || b.updated_at - a.updated_at)
    .slice(0, MAX_SESSIONS);
}

function broadcastMsg(msg) {
  const text = JSON.stringify(msg);
  for (const s of clients) sendFrame(s, text);
}

/* Recompute the display state and push it if it changed. */
function resolveAndBroadcast(now) {
  sweepStale(now);
  const display = currentDisplay();
  if (display === lastDisplay) return;
  const dom = dominantSession();
  lastDisplay = display;
  console.log(`[bridge] state: ${display}${dom ? ` (session: ${dom.session_id || '?'}, raw: ${dom.state})` : ''}`);
  broadcastMsg({ version: 'v1', type: 'display', timestamp: now, state: display });
}

function sendSnapshot(socket, now) {
  const msg = {
    version: 'v1',
    type: 'snapshot',
    timestamp: now,
    display: lastDisplay,
    sessions: sortedRecords(),
  };
  sendFrame(socket, JSON.stringify(msg));
}

/* ---------------- permission lifecycle (ticket 08) ---------------- */

function settlePermission(p, decision) {
  if (pendingPermission !== p) return;
  clearTimeout(p.timer);
  pendingPermission = null;
  broadcastMsg({
    version: 'v1',
    type: 'permission_resolved',
    timestamp: Date.now(),
    permission_id: p.id,
    decision,
  });
  p.resolve(decision);
  console.log(`[bridge] permission ${p.id}: ${decision}`);
  resolveAndBroadcast(Date.now()); // attention lifts, dominant recomputed
  pumpPermission();
}

function pumpPermission() {
  if (pendingPermission || permissionQueue.length === 0) return;
  const p = (pendingPermission = permissionQueue.shift());
  console.log(`[bridge] permission ${p.id}: asking board (tool: ${p.tool})`);
  broadcastMsg({
    version: 'v1',
    type: 'permission',
    timestamp: Date.now(),
    permission_id: p.id,
    tool: p.tool,
    hint: p.hint,
  });
  resolveAndBroadcast(Date.now()); // push forced attention
  p.timer = setTimeout(() => settlePermission(p, 'ask'), PERMISSION_TIMEOUT_MS);
}

/* Board → bridge text frame: {"type":"permission_response", permission_id,
 * decision: "once"|"deny"} (buddy vocabulary: once = allow this one). */
function handlePermissionResponse(msg) {
  if (msg.type !== 'permission_response') return;
  const p = pendingPermission;
  if (!p || msg.permission_id !== p.id) return;
  const decision = msg.decision === 'once' ? 'allow' : msg.decision === 'deny' ? 'deny' : null;
  if (!decision) return;
  settlePermission(p, decision);
}

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

/* Frame parser for client→server (masked frames). Only handles what the
 * ESP32 ws_client actually sends: ping and close. */
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
        // ESP32 text messages: permission responses (ticket 08).
        try {
          handlePermissionResponse(JSON.parse(payload.toString()));
        } catch {
          /* non-JSON from the board: ignore */
        }
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
      if (!isValidState(json.state)) {
        res.writeHead(400, { 'Content-Type': 'text/plain' }).end('invalid state');
        return;
      }
      const now = Date.now();
      const id = json.session_id || 'default';
      const basename = String(json.basename || '').slice(0, 31);

      if (json.state === 'sleeping') {
        // SessionEnd: leave the active list immediately (Clawd semantics)
        if (sessions.has(id)) {
          sessions.delete(id);
          console.log(`[bridge] session ended: ${id}`);
          broadcastMsg({ version: 'v1', type: 'session_deleted', timestamp: now, session_id: id });
        }
        resolveAndBroadcast(now);
        res.writeHead(200, { 'Content-Type': 'text/plain' }).end('ok');
        return;
      }

      sessions.set(id, { payload: json, basename, updatedAt: now });
      console.log(`[bridge] state: ${json.state} (session: ${id}, raw: ${json.state})`);
      broadcastMsg({
        version: 'v1',
        type: 'state',
        timestamp: now,
        ...sessionRecord(id, sessions.get(id)),
      });
      resolveAndBroadcast(now);
      res.writeHead(200, { 'Content-Type': 'text/plain' }).end('ok');
    });
    return;
  }

  if (req.method === 'POST' && req.url === '/permission') {
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
      const entry = {
        id: String(json.permission_id || crypto.randomUUID()),
        tool: String(json.tool || '').slice(0, 64),
        hint: String(json.hint || '').replace(/["\\\n\r\t]/g, ' ').slice(0, 255),
        resolve: (decision) => {
          if (res.writableEnded) return; // the hook hung up — nothing to answer
          res.writeHead(200, { 'Content-Type': 'application/json' })
            .end(JSON.stringify({ decision }));
        },
      };
      permissionQueue.push(entry);
      pumpPermission();
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
    const now = Date.now();
    resolveAndBroadcast(now); // sweep + display catch-up for existing clients
    sendSnapshot(socket, now); // new client gets the full picture
    // re-push a pending permission: the board drops it when the link flaps,
    // and without this the request would silently time out to ask
    if (pendingPermission) {
      const p = pendingPermission;
      sendFrame(socket, JSON.stringify({
        version: 'v1',
        type: 'permission',
        timestamp: Date.now(),
        permission_id: p.id,
        tool: p.tool,
        hint: p.hint,
      }));
      console.log(`[bridge] permission ${p.id}: re-pushed to reconnected board`);
    }

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
  console.log('[bridge] POST /state      → ingest agent state (Clawd vocabulary)');
  console.log('[bridge] POST /permission → blocking board approval (allow/deny/ask)');
  console.log('[bridge] WS   /pet        → snapshot + session list pushes to ESP32');
  setInterval(() => resolveAndBroadcast(Date.now()), SWEEP_INTERVAL_MS);
});
