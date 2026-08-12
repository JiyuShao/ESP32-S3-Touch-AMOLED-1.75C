# Research: Codex Pet / codex-pets

Research date: 2026-08-12. Sources are community GitHub repos; there is **no single official "codex-pets" repository** — "Codex Pet" is a built-in feature of the OpenAI Codex desktop app, and the ecosystem around it is community-driven.

## 1. What is Codex Pet

A desktop pet feature inside the **OpenAI Codex desktop app**: an animated mascot shown in/on the Codex window that reacts to the agent's activity (idle, working, waiting for approval, task success/failure, reviewing). Users can install custom pets as a pet *package*: a directory `~/.codex/pets/<pet-id>/` containing exactly two files — `pet.json` (metadata) + `spritesheet.webp` (animation atlas). Codex reads local pet packages; the pet UI is rendered by the app itself.

Community ecosystem: installers/galleries ([lencx/pet](https://github.com/lencx/pet), [legeling/awesome-codex-pet](https://github.com/legeling/awesome-codex-pet), [crafter-station/petdex](https://github.com/crafter-station/petdex)), renderers ([backnotprop/codex-pets-react](https://github.com/backnotprop/codex-pets-react)), atlas-build pipelines ([multicosphy/Project-Xiaodouni-Codex-Pet](https://github.com/multicosphy/Project-Xiaodouni-Codex-Pet), [leanyu165-gif/mypet-codex-pet](https://github.com/leanyu165-gif/mypet-codex-pet)), and hardware ports (see §5).

## 2. Animation assets — spritesheet atlas format

- **Files**: `spritesheet.webp` (WebP; PNG accepted by some tools) + `pet.json`, both in `~/.codex/pets/<pet-id>/` (Windows: `%USERPROFILE%\.codex\pets\`; `CODEX_HOME` overrides).
- **Classic atlas**: **1536×1872 px, 8 columns × 9 rows grid, cell = 192×208 px**.
- **V2 atlas**: 1536×2288 px, 8×11 grid, same 192×208 cells (rows 9–10 left for the consuming client; used for gaze/视线 states in [mypet-codex-pet](https://github.com/leanyu165-gif/mypet-codex-pet)).
- Each row = one animation; max 8 frames per row (one per column), unused cells transparent.
- **Color depth**: not specified in any spec/README. Source art is typically RGB+alpha; WebP encodes with alpha. No indexed/palette constraint.
- `pet.json` schema (verified from [lencx/pet airi pet.json](https://raw.githubusercontent.com/lencx/pet/main/codex/airi/pet.json)):
  ```json
  { "id": "airi", "displayName": "Airi", "description": "...", "spritesheetPath": "spritesheet.webp" }
  ```
  - `id` must match the install directory name, else Codex ignores the pet.
  - File must be UTF-8 **without BOM** — a BOM causes Codex to ignore the pet.
  - `animations.loop` exists in the engine but is internal; adding it to a custom pet.json **breaks loading** — never use.

## 3. State model — the 9 animation rows (fixed)

| Row | State | Meaning / when shown |
|-----|-------|---------------------|
| 0 | `idle` | engine's only looping state |
| 1 | `running-right` | move right |
| 2 | `running-left` | move left |
| 3 | `waving` | interact |
| 4 | `jumping` | task complete |
| 5 | `failed` | task failed |
| 6 | `waiting` | awaiting confirmation / waiting on user |
| 7 | `running` | working / delivering |
| 8 | `review` | reviewing / thinking |

Frame counts per row are per-pet (e.g. 6/8/8/4/5/8/6/6/6 in mypet-codex-pet). Engine behavior (non-configurable): sustained states (`running`, `review`, `waiting`) play once, then fall back to `idle`.

## 4. How it connects to OpenAI/Codex

- **In-app**: the Codex app itself renders the pet and maps agent lifecycle events (tool calls, failures, waiting, review) to the rows at render time. No external connection — the "connection" is internal to the desktop app. Pets are purely local files; the app picks them up on restart.
- **External/hardware monitoring** (how third parties get Codex state):
  - [Seeed-Solution/vibe-pet](https://github.com/Seeed-Solution/vibe-pet): **Hooks + JSONL session monitoring** of Codex logs; maps states (thinking, tool use, waiting for approval, completed, error) to pet animations; pushes small payloads over **BLE** to hardware.
  - [LospAgile/Codex_buddy_Cardputer_ADV](https://github.com/LospAgile/Codex_buddy_Cardputer_ADV): macOS Python daemon + Rust menu bar app; installs a managed Codex `PermissionRequest` **hook** into `~/.codex/config.toml` (delimited `# BEGIN/END CODEX BUDDY DESKTOP HOOK`), plus `codex-buddy start` launching Codex CLI with a temporary approval hook; syncs to the device over BLE or WiFi (port 47392), heartbeat-based. Mirrors compact session activity labeled `user` / `Agent` / `tool`.

## 5. Hardware targets (existing ESP32-S3 ports — precedent for this project)

| Project | Device | Display | Notes |
|---|---|---|---|
| [Codex Buddy Cardputer ADV](https://github.com/LospAgile/Codex_buddy_Cardputer_ADV) | M5Stack Cardputer ADV (**ESP32-S3**) | firmware-rendered sprite, frames downscaled 192×208 → **72×78**, 57 frames | pet compiled into firmware via `tools/generate_pet_sprite_asset.py`; PlatformIO; BLE + WiFi bridge; approve/deny buttons; MIT |
| [Vibe Pet](https://github.com/Seeed-Solution/vibe-pet) (Seeed) | Wio Terminal, SenseCAP Indicator (ESP32-S3 + RP2040, ST7701S RGB 480×480 touch), ESP32-S3 dev kit | **LVGL character rendering**; BLE | desktop app monitors agents, renders on device; avatars from petdex |

## 6. Compatibility with Clawd

- The **Codex pet format is the de-facto shared convention** across coding-agent companion pets: Claude Code companions reuse it as-is — [clawdex](https://github.com/danielkempe/clawdex) ("Codex-pet-compatible companion overlay for Claude Code", reads `~/.codex/pets/` + `~/.clawdex/pets/`, ships `hatch-pet` format reference + `validate_atlas.py`), [pet4claude](https://codeberg.org/xchacha20-poly1305/pet4claude) (reuses Codex pet format), [abpets](https://www.npmjs.com/package/abpets) (installs into `~/.codex/pets/` and `~/.agentbro/pets/` for Claude Code/Codex/Cursor/Copilot etc.).
- "Clawd" *proper* ([Zlaxrr/clawd-pet](https://github.com/Zlaxrr/clawd-pet)) is a separate Windows fan project: **does NOT use the codex-pet format** — config `clawd.json`, sprites fetched from claude.ai at first run, state via Claude Code hooks. No `~/.claude/pets` convention exists anywhere.
- **Bottom line**: if this project wants Clawd + Codex pets to share an asset pipeline, the 192×208 / 8×9 / row-0–8 action mapping / `pet.json` 4-field schema is the shared convention to target.

## Key numbers cheat-sheet

- Cell 192×208; classic atlas 1536×1872 (8×9); V2 1536×2288 (8×11)
- 9 actions, fixed row order: idle, running-right, running-left, waving, jumping, failed, waiting, running, review
- ≤8 frames per row; max 57 frames per pet
- pet.json: `id`, `displayName`, `description`, `spritesheetPath`; UTF-8 no BOM; `id` = dir name
- Install path: `~/.codex/pets/<id>/` (or `%CODEX_HOME%\pets\<id>\`)
