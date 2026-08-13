#include "pet_bridge.h"

#include <cstring>

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

} // namespace

bool PetBridge::onWsMessage(const char *payload, int len, uint32_t now_ms)
{
    char state_str[32];
    if (!extractString(payload, len, "state", state_str, sizeof(state_str))) {
        return false;
    }
    AgentState state;
    if (!parseState(state_str, strlen(state_str), &state)) {
        return false;
    }

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
