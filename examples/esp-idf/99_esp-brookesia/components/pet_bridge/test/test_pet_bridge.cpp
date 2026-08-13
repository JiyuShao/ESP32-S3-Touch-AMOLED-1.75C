/* Host test for pet_bridge state model — no ESP deps.
 * Run: g++ -std=c++17 -Wall test_pet_bridge.cpp ../pet_bridge.cpp -o test && ./test */
#include "../pet_bridge.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace pet_bridge;

static std::vector<AgentState> fired;

int main(void)
{
    PetBridge b;
    b.setStatusCallback([](const AgentStatus &s) { fired.push_back(s.state); });

    /* parse + callback on change, no callback on repeat */
    assert(b.onWsMessage("{\"state\":\"thinking\",\"session_id\":\"s1\"}", 40, 0));
    assert(b.status().state == PET_STATE_THINKING);
    assert(strcmp(b.status().session_id, "s1") == 0);
    assert(fired == std::vector<AgentState>({ PET_STATE_THINKING }));

    assert(b.onWsMessage("{\"state\":\"thinking\"}", 20, 100));
    assert(fired.size() == 1); // unchanged state, no callback

    /* alias + session_id preserved when absent */
    assert(b.onWsMessage("{\"state\":\"failed\"}", 19, 200));
    assert(b.status().state == PET_STATE_ERROR);
    assert(strcmp(b.status().session_id, "s1") == 0);
    assert(fired.back() == PET_STATE_ERROR);

    /* unknown state → false, unchanged */
    assert(!b.onWsMessage("{\"state\":\"juggling\"}", 22, 300));
    assert(b.status().state == PET_STATE_ERROR);
    assert(fired.back() == PET_STATE_ERROR);

    /* malformed payloads must not crash or change state */
    assert(!b.onWsMessage("", 0, 310));
    assert(!b.onWsMessage("{", 1, 320));
    assert(!b.onWsMessage("{}", 2, 330));
    assert(!b.onWsMessage("not json at all", 16, 340));
    assert(!b.onWsMessage("{\"state\":", 9, 350));
    assert(!b.onWsMessage("{\"state\":\"\"}", 14, 360));
    assert(b.status().state == PET_STATE_ERROR);

    /* idle timeout: still active before 10 s, IDLE after */
    assert(b.onWsMessage("{\"state\":\"working\"}", 20, 1000));
    assert(b.status().state == PET_STATE_WORKING);
    b.tick(1000 + 9999);
    assert(b.status().state == PET_STATE_WORKING);
    b.tick(1000 + 10000);
    assert(b.status().state == PET_STATE_IDLE);

    /* DISCONNECTED does not idle out */
    b.onConnectionChanged(false, 20000);
    assert(b.status().state == PET_STATE_DISCONNECTED);
    b.tick(600000);
    assert(b.status().state == PET_STATE_DISCONNECTED);

    /* reconnect → IDLE */
    b.onConnectionChanged(true, 700000);
    assert(b.status().state == PET_STATE_IDLE);

    /* initial state is DISCONNECTED */
    PetBridge fresh;
    assert(fresh.status().state == PET_STATE_DISCONNECTED);

    printf("pet_bridge: all checks passed (%zu callbacks)\n", fired.size());
    return 0;
}
