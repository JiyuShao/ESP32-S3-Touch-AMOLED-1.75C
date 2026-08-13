/* Unit checks for priority resolution — no deps.
 * Run: node test/priority-test.js */
const assert = require('assert');
const { getStatePriority, isValidState, resolveDominantState, resolveDisplayState } = require('../priority');

/* resolveDominantState: priority order + tie behavior */
assert.strictEqual(resolveDominantState([{ state: 'thinking' }, { state: 'working' }]).state, 'working');
assert.strictEqual(resolveDominantState([{ state: 'working' }, { state: 'thinking' }]).state, 'working');
assert.strictEqual(resolveDominantState([{ state: 'error' }, { state: 'working' }]).state, 'error');
assert.strictEqual(resolveDominantState([{ state: 'notification' }, { state: 'error' }]).state, 'error');
assert.strictEqual(resolveDominantState([{ state: 'sweeping' }, { state: 'attention' }]).state, 'sweeping');
assert.strictEqual(resolveDominantState([{ state: 'carrying' }, { state: 'juggling' }]).state, 'carrying'); // tie → first
assert.strictEqual(resolveDominantState([{ state: 'thinking' }, { state: 'roam' }]).state, 'thinking');
assert.strictEqual(resolveDominantState([{ state: 'sleeping' }, { state: 'thinking' }]).state, 'thinking');

/* unknown states count as priority 0 — never beat the sleeping baseline */
assert.strictEqual(getStatePriority('bogus'), 0);
assert.strictEqual(resolveDominantState([{ state: 'bogus' }, { state: 'sleeping' }]).state, 'sleeping');
assert.strictEqual(resolveDominantState([{ state: 'bogus' }]).state, 'sleeping');

/* empty / malformed input */
assert.strictEqual(resolveDominantState([]), null);
assert.strictEqual(resolveDominantState([null, {}, { state: 42 }]).state, 'sleeping');
assert.strictEqual(resolveDisplayState([]), 'idle');
assert.strictEqual(resolveDisplayState([null, {}]), 'idle');

/* validation */
assert.ok(isValidState('error'));
assert.ok(!isValidState('bogus'));
assert.ok(!isValidState(undefined));

/* display mapping */
assert.strictEqual(resolveDisplayState([{ state: 'sleeping' }]), 'idle');
assert.strictEqual(resolveDisplayState([{ state: 'roam' }]), 'idle');
assert.strictEqual(resolveDisplayState([{ state: 'notification' }]), 'attention');
assert.strictEqual(resolveDisplayState([{ state: 'attention' }]), 'attention');
assert.strictEqual(resolveDisplayState([{ state: 'carrying' }]), 'working');
assert.strictEqual(resolveDisplayState([{ state: 'juggling' }]), 'working');
assert.strictEqual(resolveDisplayState([{ state: 'sweeping' }]), 'working');
assert.strictEqual(resolveDisplayState([{ state: 'error' }]), 'error');
assert.strictEqual(resolveDisplayState([{ state: 'thinking' }]), 'thinking');
assert.strictEqual(resolveDisplayState([{ state: 'bogus' }]), 'idle');

console.log('priority: all checks passed');
