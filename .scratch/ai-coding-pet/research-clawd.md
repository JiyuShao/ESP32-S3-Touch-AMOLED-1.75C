# Research: Clawd on Desk

Source: https://github.com/rullerzhou-afk/clawd-on-desk (branch `main`, ~1938 commits, 5.9k stars, AGPL-3.0; artwork NOT covered by AGPL). Unofficial fan project; the crab character belongs to Anthropic.

**TL;DR**: Clawd is a **desktop Electron pet, not an embedded project** — no MCU/display target. The portable parts for an ESP32-S3 port are: (1) a simple HTTP event protocol (`POST /state`), (2) a small priority-ordered state machine (12 states), (3) a JSON theme manifest mapping states to animation files. All three are well-documented and trivially translatable to embedded.

---

## 1. Agent state model

### State list and priority (authoritative, `src/state-priority.js`)

```js
const STATE_PRIORITY = Object.freeze({
  error: 8,
  notification: 7,
  sweeping: 6,
  attention: 5,
  carrying: 4,
  juggling: 4,
  working: 3,
  thinking: 2,
  idle: 1,
  roam: 1,
  sleeping: 0,
});
```

```js
const SLEEP_SEQUENCE_STATES = ["yawning", "dozing", "collapsing", "sleeping", "waking"];
const ONESHOT_STATE_NAMES = ["attention", "error", "sweeping", "notification", "carrying"];
```

States unknown to the table get priority 0. 12 "logical" states per README: idle, thinking, typing (working tier 1), building (working tier 3), subagent groove / juggling, error, happy (attention), notification, sweeping, carrying, sleeping.

### Event → state mapping (from `hooks/clawd-hook.js`, verbatim)

```js
const EVENT_TO_STATE = {
  SessionStart: "idle",
  SessionEnd: "sleeping",
  UserPromptSubmit: "thinking",
  PreToolUse: "working",
  PostToolUse: "working",
  PostToolUseFailure: "error",
  Stop: "attention",
  StopFailure: "error",
  ApiError: "error",
  SubagentStart: "juggling",
  SubagentStop: "working",
  PreCompact: "sweeping",
  PostCompact: "thinking",
  Notification: "notification",
  Elicitation: "notification",
  WorktreeCreate: "carrying",
};
```

Behavioral refinements (same file): `PreToolUse` with `tool_name === "Task"` is synthesized into `SubagentStart` (juggling); `SessionEnd` with `source === "clear"` becomes sweeping; `PostCompact` with `trigger === "manual"` becomes idle; `Stop` may be upgraded to `ApiError` if the transcript tail shows an API error. `PermissionRequest` is handled by the HTTP hook (blocking), not the command hook.

### Multi-session resolution (`src/state-priority.js`, verbatim)

```js
function resolveDominantSessionState(sessions, options = {}) {
  const statePriority = options.statePriority || STATE_PRIORITY;
  let best = "sleeping";
  let hasSession = false;
  let hasNonHeadless = false;
  const latestLocalCodexProcessIds = buildLatestLocalCodexProcessIds(sessions);

  for (const [id, session] of normalizeSessionsIterable(sessions)) {
    hasSession = true;
    if (session && session.headless) continue;
    if (isSupersededLocalCodexProcessSession(id, session, latestLocalCodexProcessIds)) continue;
    hasNonHeadless = true;
    const state = session && session.state;
    if (getStatePriority(state, statePriority) > getStatePriority(best, statePriority)) best = state;
  }

  if (!hasSession || !hasNonHeadless) return "idle";
  return best;
}
```

Higher priority wins; ties keep the earlier session (strict `>`). `resolveDisplayStateFromSessions` wraps it: dominant state → forced to `notification` if `permissionLocked` → overridden by update-visual only if strictly higher priority. Headless sessions (child sessions, e.g. opencode `task` tool) are skipped.

### Per-session state machine (`src/state.js`)

- Sessions in a `Map` keyed by sessionId, `MAX_SESSIONS = 20`, eviction prefers non-ack-pending sessions.
- Session fields: `agentId`, `profileId`, `rawSessionId`, `host`, `wslDistro`, `platform`, `model`, `state`, `updatedAt`, `displayHint`, `recentEvents`, `resumeState` (pre-juggling state, restored on `SubagentStop`), `sourcePid`, `cwd`, `editor`, `pidChain`, `contextUsage`, `lastToolName`, `transcriptPath`, `assistantLastOutput`, etc.
- `pendingState` preemption: a lower-priority state is rejected if a higher one is already queued.
- Stale sweep (`cleanStaleSessions`): decisions from `getStaleSessionDecision` → delete (stale-detached) or set `idle`; empty map → `setState("idle")`.
- Completion debounce (~#406 Stop debounce), Codex exit probes (1000→15000ms), AskUserQuestion transcript probes.

### State machine for the pet window (`src/main.js`)

`applyState(resolved, getSvgOverride(resolved))` — resolved state from `resolveDisplayState()`, then per-theme timings: `minDisplay` (attention 4000, error 5000, sweeping 5500, notification 5000, carrying 3000, working/thinking 1000 ms) and `autoReturn` (error 5000 → idle, sweeping 300000 → idle, dizzy 6000, etc.) from `themes/clawd/theme.json`.

---

## 2. PC-side architecture

**No daemon separate from the app, no CLI wrapper.** Clawd is an Electron app; the *agent side* is hook scripts that Clawd installs into each agent's config. Three ingestion mechanisms + one for custom apps:

1. **Command hooks** (most agents): zero-dependency Node script reads the agent's hook JSON from stdin, maps vendor event → canonical event, POSTs to the local Clawd HTTP server. Script exits fast; `POST` timeout is 100 ms (1500 ms for `Stop`); connection-refused = Clawd not running = fail silently, never penalize the agent.
   - Claude Code: `hooks/clawd-hook.js` installed into `~/.claude/settings.json` → `settings.hooks[event].push({ matcher: "", hooks: [{ type: "command", command: "<node> <clawd-hook.js> <EventName>", async: true, timeout: 5 }] })`. Installed events: SessionStart, SessionEnd, UserPromptSubmit, PreToolUse, PostToolUse, PostToolUseFailure, Stop, SubagentStart, SubagentStop, Notification, Elicitation; version-gated PreCompact/PostCompact (≥2.1.76) and StopFailure (≥2.1.78). `WorktreeCreate` was removed (issue #127 — broke `claude -w`).
   - Other hook-based agents: Copilot, Cursor, Gemini CLI, Antigravity (state events only — intentionally no PreToolUse; permissions stay native), Kimi (`~/.kimi/config.toml`, 13 hook events), ZCode (`~/.zcode/cli/config.json`, state-only), CodeBuddy/WorkBuddy, Qwen Code, QwenWork, Qoder, Kiro (injected per-custom-agent config), Reasonix, CodeWhale, Hermes (Python plugin, synchronous POSTs).
2. **HTTP permission hook** (Claude Code, CodeBuddy; Codex uses official PermissionRequest command hook): `{ type: "http", url: "http://127.0.0.1:23333/permission", timeout: 600 }` — blocking: Clawd shows an allow/deny bubble, responds `{ behavior }`; the agent executes or not. Auto-approve policies, Telegram/Feishu remote approval are layered on this.
3. **JSONL polling (fallback only)**: Codex CLI — official hooks primary, `~/.codex/sessions/YYYY/MM/DD/rollout-*.jsonl` incremental tail as fallback (`agents/codex-log-monitor.js`).
4. **In-process plugins** (opencode, MiMo Code): Bun runtime plugins running inside the agent process, ~0ms latency, fire-and-forget `POST /state`. OpenClaw = plain ESM default object, allowlist fields only. Pi = global extension `~/.pi/agent/extensions/clawd-on-desk` (state-only, YOLO permissions preserved).

### Event pipeline (from `docs/project/agent-runtime-architecture.md`, verbatim chain)

```
agent event → hook script → HTTP POST 127.0.0.1:23333/state { state, session_id, event, source_pid, cwd }
→ src/server.js (HTTP shell) → src/server-route-state.js → src/agent-runtime-main.js
→ src/state.js (state machine: multi-session + priority + min display + sleep sequence)
→ IPC state-change → src/renderer.js (preloaded <object> SVG, crossfade switching, eye tracking)
```

Per-agent gates in `src/agent-gate.js` reading `prefs.agents[id].integrationInstalled / .enabled / .permissionsEnabled`. Recent-event ring in `server-hook-events.js` bucketed by resolved agent id; forged custom IDs land in a fixed `rejected-custom` bucket.

---

## 3. Communication protocol (PC ↔ Clawd)

### Local HTTP (the main protocol — `docs/guides/custom-agent-http.md`)

- **One route: `POST http://127.0.0.1:<runtime-port>/state`**, `Content-Type: application/json`. `/permission` is closed to custom agents (returns 204 no-decision).
- Port: Clawd binds 127.0.0.1 only, first available in **23333–23337**. Never hard-code: while running it writes `~/.clawd/runtime.json` = `{"app": "clawd-on-desk", "port": N}`. Re-read per sender process; treat missing file / refused connection as Clawd offline, fail open.
- Body limit 16 KiB. Required (all strings): `agent_id` (must match a registered custom agent), `session_id` (stable per conversation; namespaced per agent, `"default"` is safely reusable), `state` (must exist in active theme, else 400), `event` (lifecycle name).
- Optional: `cwd`, `tool_name`, `tool_use_id`, `source_pid`, `agent_pid`, `pid_chain` (int[]), `platform`, `editor` (`code`|`cursor`), `headless` (bool). No secrets/prompts/tool inputs ever.
- State/event pairs: `idle`↔`SessionStart`/`SessionEnd` (end removes session), `thinking`↔`UserPromptSubmit`, `working`↔`PreToolUse`/`PostToolUse`, `juggling`↔`SubagentStart`, `error`↔`PostToolUseFailure`, `attention`↔`Stop`, `notification`↔`Notification`.
- Responses: 200 (accepted; suppressed visible reaction under DND), 204 (agent disabled/unknown or closed route), 400 (malformed/unknown state), 413 (>16 KiB), connection failure (not running).
- Custom agents are **state-only in v1** — no permission protocol, no blocking.

### LAN WebSocket (mobile PWA companion, `docs/mobile-protocol-v1.md`)

- Opt-in read-only LAN preview. Plaintext WS at `0.0.0.0:<port>/ws?token=<hex>` + HTTP static `/mobile/*`. **No TLS, LAN only.**
- Token: 32-char hex, once, at `~/.clawd/mobile-token.json`; invalid token closes with code 1008. `/api/connection-info` never returns the token.
- Envelope: `{ "version": "v1", "type": "snapshot|state|session_deleted", "timestamp": <unix ms> }`. `snapshot` on connect (sessions keyed by id), `state` incremental, `session_deleted`. Client→server: no write messages (rate-limited then ignored); only `ping`→`pong`. Bridge polls desktop session cache every 2 s (eventually consistent). 10 max clients, 60 inbound msgs/60 s/client.

---

## 4. Animation model

**No sprite sheets, no frame format.** Themes are directories with `theme.json` + per-state animation files (SVG with embedded SMIL animations; GIF/APNG also supported; PNG/JPG for static). `src/animation-cycle.js` probes each file's cycle duration (SMIL tags / GIF frame delays / APNG) to know how long an animation runs.

`themes/clawd/theme.json` structure (16 states, each maps to 1+ SVG files):

```json
"states": {
  "idle": ["clawd-idle-follow.svg"],
  "roam": ["clawd-mini-crabwalk.svg"],
  "thinking": ["clawd-working-thinking.svg"],
  "working": ["clawd-working-typing.svg"],
  "juggling": ["clawd-headphones-groove.svg"],
  "sweeping": ["clawd-working-sweeping.svg"],
  "error": ["clawd-error.svg"],
  "attention": ["clawd-happy.svg"],
  "notification": ["clawd-notification.svg"],
  "carrying": ["clawd-working-carrying.svg"],
  "sleeping": ["clawd-sleeping.svg"],
  "yawning": ["clawd-idle-yawn.svg"],
  "dozing": ["clawd-idle-doze.svg"],
  "collapsing": ["clawd-collapse-sleep.svg"],
  "waking": ["clawd-wake.svg"],
  "dizzy": ["clawd-dizzy.svg"]
}
```

Key mechanisms:
- **Tiered variants by live session count** (the "1 session = typing, 2 = groove, 3+ = building" behavior):
```json
"workingTiers": [
  { "minSessions": 3, "file": "clawd-working-building.svg" },
  { "minSessions": 2, "file": "clawd-headphones-groove.svg" },
  { "minSessions": 1, "file": "clawd-working-typing.svg" }
],
"jugglingTiers": [
  { "minSessions": 2, "file": "clawd-working-juggling.svg" },
  { "minSessions": 1, "file": "clawd-headphones-groove.svg" }
]
```
- **Idle rotation**: `idleAnimations: [{ file, duration }]` — look (6.5 s), bubble (13.5 s), reading (14 s).
- **Timings**: `minDisplay` / `autoReturn` per state (see §1), `yawnDuration: 3000`, `wakeDuration: 1500`, `mouseIdleTimeout: 20000`, `mouseSleepTimeout: 60000`, `deepSleepTimeout: 600000` (10 min).
- **Eye tracking**: `eyeTracking: { enabled, states: ["idle","dozing","mini-idle"], ids: {eyes, body, shadow}, maxOffset: 3, ... }` — eyes/body/shadow elements moved by mouse position.
- **Reactions** (pointer): drag, clickLeft/Right, annoyed, double-click file sequences with durations.
- **Mini mode**: separate `miniMode.states` set (mini-idle/alert/happy/enter/peek/working/crabwalk/sleep) for edge-docked half-body mode.
- **Hit boxes** for click detection; `sounds: { complete, confirm }`; `viewBox`; `displayHintMap` (e.g. "checking" → debugger SVG).
- Built-in themes: clawd (crab), calico (cat), cloudling. Custom themes = same manifest format; minimum viable theme = 1 idle SVG + 7 animated files. Also imports Codex Pet zip packs (`src/codex-pet-importer.js`).

---

## 5. Multi-agent support

- **~24 agents** supported simultaneously: Claude Code, Codex CLI, Copilot CLI, Gemini CLI, Antigravity CLI, Cursor Agent, CodeBuddy, WorkBuddy, Kiro, Kimi, Qwen Code, QwenWork, Qoder, QoderWork, ZCode, CodeWhale, opencode, MiMo Code, Pi, OpenClaw, Hermes, Reasonix + custom HTTP agents.
- "Run all agents simultaneously; Clawd tracks each session independently." Sessions resolve to highest-priority state (§1). Claude Code and Codex auto-sync on fresh install; everything else opt-in from Settings → Agents.
- Integration kinds vary per agent: full (hooks + permission bubbles: Claude Code, Codex), hooks state-only (Antigravity, ZCode, QwenWork, Pi — permissions stay in the agent's native UI), plugins (opencode/MiMo in-process), JSONL fallback (Codex), custom HTTP (state-only, `agent_id` must be registered; unknown ids → 204 and never affect state).
- Subagent awareness: `SubagentStart` → juggling; per-theme asset choice by live subagent count (1 = headphones groove, 2+ = three-ball juggling for Clawd; cat/cloudling use juggling at 1, conducting at 2+).
- Per-agent gate in `src/agent-gate.js`: `integrationInstalled` (Clawd maintains the local hook/plugin) vs `enabled` (process events) vs `permissionsEnabled`.
- Session dashboard + HUD with per-agent display names (`src/agent-display-name.js`), process-liveness detection, startup recovery (hook posts persist evidence to disk via `updateRecoveryLeaseFromStateBody` so a restarting Clawd can restore sessions).

---

## 6. Hardware

**None.** Clawd targets Windows 11, macOS, Ubuntu/Linux desktops — an Electron app. There is no MCU, display driver, or firmware anywhere. This is a *software-only* reference.

### What transfers to an ESP32-S3 AMOLED pet

- **The `/state` protocol is the port surface**: a device-side TCP/HTTP client (or a tiny bridge daemon on a PC) POSTing `{ agent_id, session_id, state, event }` to the PC-side collector is exactly how Clawd's custom agents work. For an ESP32 you'd likely invert it: a PC-side lightweight hook collector (or the Clawd HTTP server itself on 23333–23337) → ESP32 over WiFi/WebSocket (the mobile-protocol-v1 WS bridge is the closer analog: LAN, token-gated, `{version, type, timestamp}` envelope).
- **The 12-state priority state machine** (§1) is directly portable — it's a few dozen lines.
- **theme.json is the animation manifest**: states → assets, tiered variants by session count, idle rotation, min-display/auto-return timings. An embedded port replaces SVG/SMIL with Lottie/sprite frames while keeping the same manifest keys (LVGL `lv_png`/sprite frames; brookesia framework could map each state key to an lv_img).
- **Tiers by session count** (1/2/3+ sessions → different working animation) is the most distinctive behavior worth keeping.

## Sources (all on branch `main`)

- `docs/guides/state-mapping.md` — event→state mapping, tier rules, per-agent mapping tables
- `docs/guides/custom-agent-http.md` — /state protocol spec
- `docs/mobile-protocol-v1.md` — LAN WebSocket PWA protocol
- `docs/project/agent-runtime-architecture.md` — full architecture + event pipeline
- `src/state-priority.js` — STATE_PRIORITY, resolveDominantSessionState (verbatim above)
- `src/state.js` — session map, merge, stale cleanup
- `src/main.js` — composition root, applyState
- `hooks/clawd-hook.js` — EVENT_TO_STATE, payload builder
- `hooks/install.js` — hook registration into `~/.claude/settings.json`
- `themes/clawd/theme.json` — animation manifest
- `docs/guides/guide-theme-creation.md`, `docs/guides/known-limitations.md` — theme format, feature-gap table
