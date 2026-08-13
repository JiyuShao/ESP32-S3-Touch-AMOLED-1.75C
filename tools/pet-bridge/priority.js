/* Clawd state priority model + display mapping (ticket 04).
 * Priority table from Clawd src/state-priority.js (see research-clawd.md). */

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

/* Clawd state → ESP32 display state (pet_bridge vocabulary on the device). */
const DISPLAY_STATE = Object.freeze({
  error: 'error',
  notification: 'attention',
  sweeping: 'working',
  attention: 'attention',
  carrying: 'working',
  juggling: 'working',
  working: 'working',
  thinking: 'thinking',
  idle: 'idle',
  roam: 'idle',
  sleeping: 'idle',
});

function getStatePriority(state) {
  return STATE_PRIORITY[state] ?? 0;
}

function isValidState(state) {
  return typeof state === 'string' && Object.hasOwn(STATE_PRIORITY, state);
}

/* Highest priority wins; ties keep the earlier session (strict >).
 * Mirrors Clawd: best starts at "sleeping", so priority-0 unknowns never win. */
function resolveDominantState(sessions) {
  let best = { state: 'sleeping' };
  for (const s of sessions) {
    if (!s || typeof s.state !== 'string') continue;
    if (getStatePriority(s.state) > getStatePriority(best.state)) best = s;
  }
  return sessions.length ? best : null;
}

/* Dominant session → display state for the ESP32. */
function resolveDisplayState(sessions) {
  const dom = resolveDominantState(sessions);
  if (!dom) return 'idle';
  return DISPLAY_STATE[dom.state] ?? 'idle';
}

module.exports = { STATE_PRIORITY, DISPLAY_STATE, getStatePriority, isValidState, resolveDominantState, resolveDisplayState };
