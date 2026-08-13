#!/usr/bin/env node
/*
 * Claude Code hook → POST /state to the Pet Bridge.
 *
 * Installed via ~/.claude/settings.json (one entry per event below).
 * Fail-open: any error (no bridge, bad stdin) exits 0 silently — the pet
 * just misses a state update.
 *
 * Event → state:
 *   SessionStart      → idle      (session appears in the pet's list)
 *   SessionEnd        → sleeping  (removes it from the list, Clawd semantics)
 *   UserPromptSubmit  → thinking  (agent is reading the prompt)
 *   PreToolUse        → working   (a tool is about to run)
 *   PostToolUse       → thinking  (tool done, agent is reasoning again)
 *   SubagentStart     → working
 *   Stop              → idle
 */

const http = require('http');

const PORT = Number(process.env.PET_BRIDGE_PORT) || 8787;

const EVENT_TO_STATE = {
  SessionStart: 'idle',
  SessionEnd: 'sleeping',
  UserPromptSubmit: 'thinking',
  PreToolUse: 'working',
  PostToolUse: 'thinking',
  SubagentStart: 'working',
  Stop: 'idle',
};

function postState(state, sessionId, event, basename) {
  const payload = JSON.stringify({
    version: 'v1',
    session_id: sessionId || 'default',
    state,
    event,
    basename,
    timestamp: Date.now(),
  });
  const req = http.request(
    {
      host: '127.0.0.1',
      port: PORT,
      path: '/state',
      method: 'POST',
      timeout: 2000,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(payload) },
    },
    (res) => res.resume(), // drain; result doesn't matter
  );
  req.on('error', () => {}); // bridge down → silent
  req.on('timeout', () => req.destroy());
  req.end(payload);
}

let input = '';
process.stdin.on('data', (d) => (input += d));
process.stdin.on('end', () => {
  let hook;
  try {
    hook = JSON.parse(input);
  } catch {
    return; // fail-open
  }
  const state = EVENT_TO_STATE[hook.hook_event_name];
  if (!state) return; // event we don't map (e.g. Notification) → skip
  const cwd = hook.cwd || '';
  const basename = cwd.split('/').filter(Boolean).pop() || '';
  postState(state, hook.session_id, hook.hook_event_name, basename);
});
