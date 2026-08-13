#pragma once

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

/* Global scope so the generated C frame table (pet_frames.c) can index by
 * PET_STATE_*. Kept C-compatible on purpose. */
enum AgentState {
    PET_STATE_IDLE = 0,
    PET_STATE_THINKING,
    PET_STATE_WORKING,
    PET_STATE_ATTENTION,
    PET_STATE_ERROR,
    PET_STATE_DISCONNECTED,
    PET_STATE_COUNT,
};

#ifdef __cplusplus

#include <functional>

namespace pet_bridge {

/* One active agent session, as reported by the PC Bridge (ticket 06). */
struct SessionEntry {
    char session_id[32] = {};
    char basename[32] = {};
    AgentState state = PET_STATE_IDLE; // display-mapped (6-state vocabulary)
    uint64_t updated_at_ms = 0;        // bridge Date.now() ms — needs 64 bit
    int priority = 0;                  // Clawd priority, for sorting (error first)
};

#define PET_BRIDGE_MAX_SESSIONS 8

struct AgentStatus {
    AgentState state = PET_STATE_DISCONNECTED;
    char session_id[32] = {};
    uint32_t last_update_ms = 0;
};

/**
 * State model fed by the PC Bridge protocol (ticket 06, Clawd-aligned):
 *   {"version":"v1","type":"snapshot",...}      full session list + display
 *   {"version":"v1","type":"state",...}         single session upsert
 *   {"version":"v1","type":"session_deleted",...}
 *   {"version":"v1","type":"display",...}       dominant display state
 *   {"state":"..."}                             legacy, treated as display
 * Pure logic, no ESP/LVGL deps — host-testable.
 */
class PetBridge {
public:
    using StatusCallback = std::function<void(const AgentStatus &)>;

    void setStatusCallback(StatusCallback cb) { _cb = std::move(cb); }

    /**
     * Parse a WS text frame and switch state if it changed.
     * @return false if the payload carries no valid state.
     */
    bool onWsMessage(const char *payload, int len, uint32_t now_ms);

    /** WS connect/disconnect transition from the transport layer. */
    void onConnectionChanged(bool connected, uint32_t now_ms);

    /** Idle timeout: 10 s without a message → IDLE. Call periodically. */
    void tick(uint32_t now_ms);

    const AgentStatus &status() const { return _status; }

    /** Active session list, sorted by priority desc (error first). */
    const SessionEntry *sessions(int *count) const
    {
        *count = _session_count;
        return _sessions;
    }

private:
    void updateState(AgentState state, uint32_t now_ms);
    bool applyDisplayState(const char *payload, int len, uint32_t now_ms);
    bool applySnapshot(const char *payload, int len, uint32_t now_ms);
    bool applySessionState(const char *payload, int len, uint32_t now_ms);
    bool applySessionDeleted(const char *payload, int len, uint32_t now_ms);
    void insertSorted(const SessionEntry &entry);
    void removeSession(const char *session_id);

    StatusCallback _cb;
    AgentStatus _status;
    SessionEntry _sessions[PET_BRIDGE_MAX_SESSIONS];
    int _session_count = 0;
};

} // namespace pet_bridge

#endif // __cplusplus
