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

struct AgentStatus {
    AgentState state = PET_STATE_DISCONNECTED;
    char session_id[32] = {};
    uint32_t last_update_ms = 0;
};

/**
 * State model fed by the PC Bridge protocol ({"state":"..."} WS messages)
 * and connection events. Pure logic, no ESP/LVGL deps — host-testable.
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

private:
    void updateState(AgentState state, uint32_t now_ms);

    StatusCallback _cb;
    AgentStatus _status;
};

} // namespace pet_bridge

#endif // __cplusplus
