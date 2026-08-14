#!/usr/bin/env node
/*
 * Claude Code PermissionRequest hook (ticket 08) — the one SYNC hook.
 *
 * Blocks the tool call, POSTs to the bridge, and waits for the board's
 * decision (allow/deny) or the bridge's fallback (ask → Claude's native
 * permission prompt). Fail-open: bridge unreachable → "ask" immediately,
 * so a missing pet never blocks or silently approves anything.
 *
 * Installed via ~/.claude/settings.json as a synchronous PermissionRequest
 * hook (timeout 300 s > bridge's 150 s hold, so the ask fallback fires
 * first inside the bridge).
 */

const http = require('http');
const crypto = require('crypto');

const PORT = Number(process.env.PET_BRIDGE_PORT) || 8787;
const BRIDGE_TIMEOUT_MS = Number(process.env.PET_BRIDGE_PERMISSION_TIMEOUT_MS) || 150 * 1000;
/* This HTTP timeout sits between the bridge's 150 s and the hook's 300 s so
 * the hook itself answers "ask" if the bridge goes mute without responding. */
const HTTP_TIMEOUT_MS = BRIDGE_TIMEOUT_MS + 120 * 1000;

function output(decision, reason) {
  process.stdout.write(
    JSON.stringify({
      hookSpecificOutput: {
        hookEventName: 'PermissionRequest',
        permissionDecision: decision,
        permissionDecisionReason: reason,
      },
    }),
  );
}

function ask(reason) {
  output('ask', reason);
  process.exit(0);
}

let input = '';
process.stdin.on('data', (d) => (input += d));
process.stdin.on('end', () => {
  let hook;
  try {
    hook = JSON.parse(input);
  } catch {
    ask('bad hook input');
    return;
  }

  /* The hint is what the board shows while asking for approval. Cap it hard:
   * the board's label buffers are small, and tool input may contain secrets
   * we don't want bouncing around the LAN unnecessarily. */
  let hint = `tool: ${hook.tool_name || 'unknown'}`;
  if (hook.tool_input) {
    const detail = JSON.stringify(hook.tool_input).slice(0, 160);
    if (detail) hint = `${hint} — ${detail}`;
  }

  const payload = JSON.stringify({
    version: 'v1',
    permission_id: crypto.randomUUID(),
    tool: String(hook.tool_name || 'tool').slice(0, 64),
    hint: hint.slice(0, 256),
    timestamp: Date.now(),
  });

  const req = http.request(
    {
      host: '127.0.0.1',
      port: PORT,
      path: '/permission',
      method: 'POST',
      timeout: HTTP_TIMEOUT_MS,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(payload) },
    },
    (res) => {
      let body = '';
      res.on('data', (c) => (body += c));
      res.on('end', () => {
        let decision = 'ask';
        try {
          decision = JSON.parse(body).decision || 'ask';
        } catch {
          /* bridge answered garbage → fall back */
        }
        if (decision !== 'allow' && decision !== 'deny') decision = 'ask';
        output(
          decision,
          decision === 'ask' ? 'bridge fell back (timeout or no board)' : 'board decision',
        );
        process.exit(0);
      });
    },
  );
  req.on('error', () => ask('bridge unreachable'));
  req.on('timeout', () => {
    req.destroy();
    ask('bridge timeout');
  });
  req.end(payload);
});
