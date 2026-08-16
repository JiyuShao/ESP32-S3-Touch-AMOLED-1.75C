#include "pet_bridge.h"

#include <cstring>
#include "esp_log.h"

namespace pet_bridge {

namespace {

constexpr uint32_t IDLE_TIMEOUT_MS = 10000;

/* State names on the wire (PC Bridge maps its 12 Clawd states onto these). */
struct StateName {
    const char *name;
    AgentState state;
};

const StateName STATE_NAMES[] = {
    { "idle", PET_STATE_IDLE },
    { "thinking", PET_STATE_THINKING },
    { "working", PET_STATE_WORKING },
    { "attention", PET_STATE_ATTENTION },
    { "waiting", PET_STATE_ATTENTION },   // alias
    { "error", PET_STATE_ERROR },
    { "failed", PET_STATE_ERROR },       // alias
    { "disconnected", PET_STATE_DISCONNECTED },
};

bool parseState(const char *s, size_t len, AgentState *out)
{
    for (const auto &sn : STATE_NAMES) {
        if (strlen(sn.name) == len && strncmp(sn.name, s, len) == 0) {
            *out = sn.state;
            return true;
        }
    }
    return false;
}

/* Extract `"key":"value"` from a JSON-ish payload. Bounds-checked; value
 * must be a plain ASCII string without escapes (all our fields qualify). */
bool extractString(const char *payload, int len, const char *key,
                   char *out, size_t out_len)
{
    size_t key_len = strlen(key);
    char search[48];
    if (key_len + 2 > sizeof(search)) {
        return false;
    }
    search[0] = '"';
    memcpy(search + 1, key, key_len);
    search[key_len + 1] = '"';
    size_t search_len = key_len + 2;

    for (int i = 0; i + (int)search_len + 2 < len; i++) {
        if (memcmp(payload + i, search, search_len) != 0) {
            continue;
        }
        // must sit at a key position: preceded by { , or whitespace — else
        // we'd match the same text used as a value (e.g. "type":"state")
        if (i > 0 && payload[i - 1] != '{' && payload[i - 1] != ',' &&
            payload[i - 1] != ' ' && payload[i - 1] != '\t') {
            continue;
        }
        // find the colon then the opening quote
        int j = i + (int)search_len;
        while (j < len && (payload[j] == ' ' || payload[j] == '\t' || payload[j] == ':')) {
            j++;
        }
        if (j >= len || payload[j] != '"') {
            return false;
        }
        j++;
        size_t n = 0;
        while (j < len && payload[j] != '"' && payload[j] != '\\' && n + 1 < out_len) {
            out[n++] = payload[j++];
        }
        out[n] = '\0';
        return n > 0;
    }
    return false;
}

/* Extract `"key":<digits>` — plain JSON number, bounds-checked. */
bool extractUint64(const char *payload, int len, const char *key, uint64_t *out)
{
    size_t key_len = strlen(key);
    char search[48];
    if (key_len + 2 > sizeof(search)) {
        return false;
    }
    search[0] = '"';
    memcpy(search + 1, key, key_len);
    search[key_len + 1] = '"';
    size_t search_len = key_len + 2;

    for (int i = 0; i + (int)search_len + 2 < len; i++) {
        if (memcmp(payload + i, search, search_len) != 0) {
            continue;
        }
        if (i > 0 && payload[i - 1] != '{' && payload[i - 1] != ',' &&
            payload[i - 1] != ' ' && payload[i - 1] != '\t') {
            continue;
        }
        int j = i + (int)search_len;
        while (j < len && (payload[j] == ' ' || payload[j] == '\t' || payload[j] == ':')) {
            j++;
        }
        if (j >= len || (payload[j] < '0' || payload[j] > '9')) {
            return false;
        }
        uint64_t v = 0;
        while (j < len && payload[j] >= '0' && payload[j] <= '9') {
            v = v * 10 + (uint64_t)(payload[j] - '0');
            j++;
        }
        *out = v;
        return true;
    }
    return false;
}

/* Insertion sort: priority desc, then updated_at desc. Stable (strict >),
 * so ties keep arrival order. n <= PET_BRIDGE_MAX_SESSIONS, trivial cost. */
void sortSessionEntries(SessionEntry *arr, int count)
{
    for (int i = 1; i < count; i++) {
        SessionEntry key = arr[i];
        int j = i - 1;
        while (j >= 0 &&
               (arr[j].priority < key.priority ||
                (arr[j].priority == key.priority && arr[j].updated_at_ms < key.updated_at_ms))) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

} // namespace

void PetBridge::insertSorted(const SessionEntry &entry)
{
    for (int i = 0; i < _session_count; i++) {
        if (strcmp(_sessions[i].session_id, entry.session_id) == 0) {
            _sessions[i] = entry;
            sortSessionEntries(_sessions, _session_count);
            return;
        }
    }
    if (_session_count >= PET_BRIDGE_MAX_SESSIONS) {
        return; // full: drop the newcomer, the high-priority ones stay
    }
    _sessions[_session_count++] = entry;
    sortSessionEntries(_sessions, _session_count);
}

void PetBridge::removeSession(const char *session_id)
{
    for (int i = 0; i < _session_count; i++) {
        if (strcmp(_sessions[i].session_id, session_id) == 0) {
            memmove(&_sessions[i], &_sessions[i + 1],
                    (size_t)(_session_count - i - 1) * sizeof(SessionEntry));
            _session_count--;
            return;
        }
    }
}

bool PetBridge::applyDisplayState(const char *payload, int len, uint32_t now_ms)
{
    char state_str[32];
    if (!extractString(payload, len, "state", state_str, sizeof(state_str))) {
        return false;
    }
    AgentState state;
    if (!parseState(state_str, strlen(state_str), &state)) {
        return false;
    }
    ESP_LOGD("pet_bridge", "disp-msg: %s", state_str);

    // session_id is optional; keep the last known one if absent
    char session[sizeof(_status.session_id)];
    if (extractString(payload, len, "session_id", session, sizeof(session))) {
        memcpy(_status.session_id, session, sizeof(session));
    }

    if (state != _status.state) {
        updateState(state, now_ms);
    }
    return true;
}

bool PetBridge::applySnapshot(const char *payload, int len, uint32_t now_ms)
{
    /* Walk the "sessions":[...] array as {..} spans (our records have no
     * nested objects, so the first '}' closes each entry). */
    int arr = -1;
    for (int i = 0; i < len; i++) {
        if (payload[i] == '[') {
            arr = i;
            break;
        }
    }
    if (arr < 0) {
        return false;
    }

    SessionEntry tmp[PET_BRIDGE_MAX_SESSIONS];
    int count = 0;
    int i = arr + 1;
    while (i < len && count < PET_BRIDGE_MAX_SESSIONS) {
        int obj = -1;
        for (; i < len; i++) {
            if (payload[i] == '{') {
                obj = i;
                break;
            }
        }
        if (obj < 0) {
            break;
        }
        int end = -1;
        for (int j = obj + 1; j < len; j++) {
            if (payload[j] == '}') {
                end = j;
                break;
            }
        }
        if (end < 0) {
            break;
        }

        SessionEntry e;
        char st[32];
        if (extractString(payload + obj, end - obj + 1, "session_id", e.session_id, sizeof(e.session_id)) &&
            extractString(payload + obj, end - obj + 1, "state", st, sizeof(st)) &&
            parseState(st, strlen(st), &e.state)) {
            if (!extractString(payload + obj, end - obj + 1, "basename", e.basename, sizeof(e.basename))) {
                e.basename[0] = '\0';
            }
            uint64_t v = 0;
            if (extractUint64(payload + obj, end - obj + 1, "updated_at", &v)) {
                e.updated_at_ms = v;
            }
            v = 0;
            if (extractUint64(payload + obj, end - obj + 1, "priority", &v)) {
                e.priority = (int)v;
            }
            tmp[count++] = e;
        }
        i = end + 1;
    }

    _session_count = count;
    sortSessionEntries(tmp, count);
    memcpy(_sessions, tmp, (size_t)count * sizeof(SessionEntry));

    /* display state applies if present and valid */
    char disp[32];
    if (extractString(payload, len, "display", disp, sizeof(disp))) {
        AgentState s;
        if (parseState(disp, strlen(disp), &s) && s != _status.state) {
            updateState(s, now_ms);
        }
    }
    return true;
}

bool PetBridge::applySessionState(const char *payload, int len, uint32_t now_ms)
{
    SessionEntry e;
    char st[32];
    if (!extractString(payload, len, "session_id", e.session_id, sizeof(e.session_id)) ||
        !extractString(payload, len, "state", st, sizeof(st)) ||
        !parseState(st, strlen(st), &e.state)) {
        return false;
    }
    if (!extractString(payload, len, "basename", e.basename, sizeof(e.basename))) {
        e.basename[0] = '\0';
    }
    uint64_t v = 0;
    if (extractUint64(payload, len, "updated_at", &v)) {
        e.updated_at_ms = v;
    }
    v = 0;
    if (extractUint64(payload, len, "priority", &v)) {
        e.priority = (int)v;
    }
    if (!extractString(payload, len, "event", e.event, sizeof(e.event))) {
        e.event[0] = '\0';
    }
    if (!extractString(payload, len, "detail", e.detail, sizeof(e.detail))) {
        e.detail[0] = '\0';
    }
    insertSorted(e);
    return true;
}

bool PetBridge::applyPermission(const char *payload, int len, uint32_t now_ms)
{
    PermissionRequest p;
    if (!extractString(payload, len, "permission_id", p.permission_id, sizeof(p.permission_id))) {
        return false;
    }
    if (!extractString(payload, len, "tool", p.tool, sizeof(p.tool))) {
        p.tool[0] = '\0';
    }
    // Cross-side contract: the bridge sanitizes the hint (strips quotes and
    // backslashes) because extractString stops at escapes — see bridge.js.
    if (!extractString(payload, len, "hint", p.hint, sizeof(p.hint))) {
        p.hint[0] = '\0';
    }
    _permission = p;
    return true;
}

bool PetBridge::applyPermissionResolved(const char *payload, int len, uint32_t now_ms)
{
    char permission_id[sizeof(_permission.permission_id)];
    if (!extractString(payload, len, "permission_id", permission_id, sizeof(permission_id))) {
        return false;
    }
    if (strcmp(permission_id, _permission.permission_id) == 0) {
        _permission = PermissionRequest{}; // resolved: nothing pending
    }
    return true;
}

bool PetBridge::applySessionDeleted(const char *payload, int len, uint32_t now_ms)
{
    char session_id[sizeof(_sessions[0].session_id)];
    if (!extractString(payload, len, "session_id", session_id, sizeof(session_id))) {
        return false;
    }
    removeSession(session_id);
    return true;
}

bool PetBridge::onWsMessage(const char *payload, int len, uint32_t now_ms)
{
    bool ok;
    char type[32];
    if (!extractString(payload, len, "type", type, sizeof(type))) {
        ok = applyDisplayState(payload, len, now_ms); // legacy {"state":...}
    } else if (strcmp(type, "snapshot") == 0) {
        ok = applySnapshot(payload, len, now_ms);
    } else if (strcmp(type, "state") == 0) {
        ok = applySessionState(payload, len, now_ms);
    } else if (strcmp(type, "session_deleted") == 0) {
        ok = applySessionDeleted(payload, len, now_ms);
    } else if (strcmp(type, "display") == 0) {
        ok = applyDisplayState(payload, len, now_ms);
    } else if (strcmp(type, "permission") == 0) {
        ok = applyPermission(payload, len, now_ms);
    } else if (strcmp(type, "permission_resolved") == 0) {
        ok = applyPermissionResolved(payload, len, now_ms);
    } else {
        return false;
    }
    // any valid message is proof of life — keep the pet awake during long
    // tool calls even when the display state itself doesn't change
    if (ok) {
        _status.last_update_ms = now_ms;
    }
    return ok;
}

void PetBridge::onConnectionChanged(bool connected, uint32_t now_ms)
{
    if (connected) {
        // link restored: leave DISCONNECTED (the bridge replays the last
        // state; if it has none, the idle timeout brings us to IDLE)
        if (_status.state == PET_STATE_DISCONNECTED) {
            updateState(PET_STATE_IDLE, now_ms);
        }
    } else {
        updateState(PET_STATE_DISCONNECTED, now_ms);
        // link lost: nobody can collect the answer anymore, so drop any
        // pending permission — otherwise the overlay would wait forever for
        // a permission_resolved that can never arrive (bridge settles ask
        // on timeout only while the link is up)
        _permission = PermissionRequest{};
    }
}

void PetBridge::tick(uint32_t now_ms)
{
    if (_status.state == PET_STATE_IDLE || _status.state == PET_STATE_DISCONNECTED) {
        return;
    }
    // int32 cast keeps the diff correct across the ~49-day lv_tick wrap
    if ((int32_t)(now_ms - _status.last_update_ms) >= (int32_t)IDLE_TIMEOUT_MS) {
        updateState(PET_STATE_IDLE, now_ms);
    }
}

void PetBridge::updateState(AgentState state, uint32_t now_ms)
{
    _status.state = state;
    _status.last_update_ms = now_ms;
    if (_cb) {
        _cb(_status);
    }
}

} // namespace pet_bridge
