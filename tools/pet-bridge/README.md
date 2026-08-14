# Pet Bridge

PC-side daemon for the AI Coding Pet: agent states in, dominant display state out to the ESP32 over WebSocket, plus board-mediated permission approval.

```
Claude Code hooks ──POST──▶ bridge.js ──WS──▶ ESP32 pet
              └────PermissionRequest (sync, 300 s)────▶ board approves/denies
```

## Run

```sh
node bridge.js
```

- `POST http://127.0.0.1:8787/state` — body `{"state":"<clawd-state>","session_id":"...",...}`.
  Valid states (Clawd vocabulary): `error notification sweeping attention carrying
  juggling working thinking idle roam sleeping`. Invalid → HTTP 400.
- `POST http://127.0.0.1:8787/permission` — body `{"permission_id":"...","tool":"Bash","hint":"..."}`.
  Blocks until the board answers (via WS) or 150 s elapse; responds
  `{"decision":"allow"|"deny"|"ask"}`. `ask` = fall back to Claude's native prompt.
  Requests queue: one is pushed to the board at a time.
- `GET ws://<pc-ip>:8787/pet` — pushes the dominant display state (`idle / thinking /
  working / attention / error`), replays it on connect, and delivers
  `permission` / `permission_resolved` pushes. The board's answer comes back as a
  masked text frame: `{"type":"permission_response","permission_id":...,"decision":"once"|"deny"}`.
- While a permission request is pending, the display state is forced to `attention`.
- Sessions expire after 5 min without updates (override: `PET_BRIDGE_SESSION_TTL_MS`);
  non-idle sessions after 30 min (`PET_BRIDGE_ACTIVE_TTL_MS`); the permission hold
  is 150 s (`PET_BRIDGE_PERMISSION_TIMEOUT_MS`).

Priority model: `priority.js` (Clawd table — error beats everything, ties keep the
earlier session). The 12 Clawd states map to the pet's 6 display states there.

## Claude Code hooks

`hooks/claude-code-hook.js` posts state on Claude Code lifecycle events. Fail-open:
if the bridge isn't running it exits silently and nothing else happens.

`hooks/claude-code-permission-hook.js` is the one SYNC hook: it blocks the tool
call, waits for the board decision or the bridge's ask fallback, and outputs
`hookSpecificOutput.permissionDecision` (allow/deny/ask). Bridge unreachable → ask
immediately.

To install, merge the block below into `~/.claude/settings.json` (adjust the path
if your checkout lives elsewhere). The 7 state events are async; the
PermissionRequest hook is synchronous with a 300 s timeout (must exceed the
bridge's 150 s hold so the ask fallback happens first):

```json
{
  "hooks": {
    "SessionStart":      [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-hook.js", "async": true, "timeout": 5 }] }],
    "SessionEnd":        [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-hook.js", "async": true, "timeout": 5 }] }],
    "UserPromptSubmit":  [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-hook.js", "async": true, "timeout": 5 }] }],
    "PreToolUse":        [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-hook.js", "async": true, "timeout": 5 }] }],
    "PostToolUse":       [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-hook.js", "async": true, "timeout": 5 }] }],
    "SubagentStart":     [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-hook.js", "async": true, "timeout": 5 }] }],
    "Stop":              [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-hook.js", "async": true, "timeout": 5 }] }],
    "PermissionRequest": [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-permission-hook.js", "timeout": 300 }] }]
  }
}
```

Event → state mapping: `SessionStart`→idle, `SessionEnd`→sleeping,
`UserPromptSubmit`→thinking, `PreToolUse`→working, `PostToolUse`→thinking,
`SubagentStart`→working, `Stop`→idle.

## Tests

```sh
node test/priority-test.js          # unit checks for priority resolution
sh test/simulate-session.sh         # two sessions + TTL against a live bridge
node test/ws-client-test.js         # WS replay/push/pong (bridge must be running)
node test/permission-test.js        # permission chain / queue / timeout (fake board)
node test/permission-e2e-test.js    # real hook process + fake board → allow/ask
```
