# Pet Bridge

PC-side daemon for the AI Coding Pet: agent states in, dominant display state out to the ESP32 over WebSocket.

```
Claude Code hooks ──POST──▶ bridge.js ──WS──▶ ESP32 pet
```

## Run

```sh
node bridge.js
```

- `POST http://127.0.0.1:8787/state` — body `{"state":"<clawd-state>","session_id":"...",...}`.
  Valid states (Clawd vocabulary): `error notification sweeping attention carrying
  juggling working thinking idle roam sleeping`. Invalid → HTTP 400.
- `GET ws://<pc-ip>:8787/pet` — pushes the dominant display state (`idle / thinking /
  working / attention / error`) and replays it on connect.
- Sessions expire after 5 min without updates (override: `PET_BRIDGE_SESSION_TTL_MS`).

Priority model: `priority.js` (Clawd table — error beats everything, ties keep the
earlier session). The 12 Clawd states map to the pet's 6 display states there.

## Claude Code hook

`hooks/claude-code-hook.js` posts state on Claude Code lifecycle events. Fail-open:
if the bridge isn't running it exits silently and nothing else happens.

To install, merge the block below into `~/.claude/settings.json` (adjust the path
if your checkout lives elsewhere):

```json
{
  "hooks": {
    "UserPromptSubmit": [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-hook.js", "async": true, "timeout": 5 }] }],
    "PreToolUse":       [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-hook.js", "async": true, "timeout": 5 }] }],
    "PostToolUse":      [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-hook.js", "async": true, "timeout": 5 }] }],
    "Stop":             [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-hook.js", "async": true, "timeout": 5 }] }],
    "SubagentStart":    [{ "hooks": [{ "type": "command", "command": "node /Users/jiyu/Code/personal/@learning/@esp/ESP32-S3-Touch-AMOLED-1.75C/tools/pet-bridge/hooks/claude-code-hook.js", "async": true, "timeout": 5 }] }]
  }
}
```

Event → state mapping: `UserPromptSubmit`→thinking, `PreToolUse`→working,
`PostToolUse`→thinking, `SubagentStart`→working, `Stop`→idle.

## Tests

```sh
node test/priority-test.js     # unit checks for priority resolution
sh test/simulate-session.sh    # two sessions + TTL against a live bridge
node test/ws-client-test.js    # WS replay/push/pong (bridge must be running)
```
