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

    /* any valid message is proof of life even when the state is unchanged:
     * 10 s after the first "working" (810000) the pet must still be working
     * because the second identical message refreshed the timeout */
    assert(b.onWsMessage("{\"state\":\"working\"}", 20, 800000));
    assert(b.onWsMessage("{\"state\":\"working\"}", 20, 800500));
    b.tick(810000);
    assert(b.status().state == PET_STATE_WORKING);
    b.tick(800500 + 10000);
    assert(b.status().state == PET_STATE_IDLE);

    /* initial state is DISCONNECTED */
    PetBridge fresh;
    assert(fresh.status().state == PET_STATE_DISCONNECTED);

    /* ---- ticket 06: Clawd-aligned session list protocol ---- */

    /* snapshot: replaces the list, applies display state */
    const char *snap =
        "{\"version\":\"v1\",\"type\":\"snapshot\",\"timestamp\":1000,\"display\":\"thinking\","
        "\"sessions\":["
        "{\"session_id\":\"e1\",\"basename\":\"proj\",\"state\":\"error\",\"updated_at\":100,\"priority\":8},"
        "{\"session_id\":\"w1\",\"basename\":\"doc\",\"state\":\"working\",\"updated_at\":200,\"priority\":3},"
        "{\"session_id\":\"t1\",\"basename\":\"app\",\"state\":\"thinking\",\"updated_at\":300,\"priority\":2}]}";
    assert(b.onWsMessage(snap, (int)strlen(snap), 1000));
    assert(b.status().state == PET_STATE_THINKING);
    int n = 0;
    const SessionEntry *sess = b.sessions(&n);
    assert(n == 3);
    assert(strcmp(sess[0].session_id, "e1") == 0); // priority 8 first
    assert(strcmp(sess[0].basename, "proj") == 0);
    assert(sess[0].state == PET_STATE_ERROR);
    assert(sess[2].priority == 2);

    /* state upsert: update in place and re-sort by priority */
    const char *st =
        "{\"version\":\"v1\",\"type\":\"state\",\"timestamp\":1100,"
        "\"session_id\":\"t1\",\"basename\":\"app\",\"state\":\"error\","
        "\"updated_at\":400,\"priority\":8}";
    assert(b.onWsMessage(st, (int)strlen(st), 1100));
    sess = b.sessions(&n);
    assert(n == 3);
    assert(strcmp(sess[0].session_id, "t1") == 0); // bumped to error → top
    assert(sess[0].state == PET_STATE_ERROR);

    /* state upsert: unknown session appends sorted (idle lands last) */
    const char *st2 =
        "{\"version\":\"v1\",\"type\":\"state\",\"timestamp\":1200,"
        "\"session_id\":\"n1\",\"basename\":\"new\",\"state\":\"idle\","
        "\"updated_at\":500,\"priority\":1}";
    assert(b.onWsMessage(st2, (int)strlen(st2), 1200));
    sess = b.sessions(&n);
    assert(n == 4);
    assert(strcmp(sess[3].session_id, "n1") == 0);

    /* session_deleted removes one entry */
    const char *del =
        "{\"version\":\"v1\",\"type\":\"session_deleted\",\"timestamp\":1300,\"session_id\":\"w1\"}";
    assert(b.onWsMessage(del, (int)strlen(del), 1300));
    sess = b.sessions(&n);
    assert(n == 3);
    for (int i = 0; i < n; i++) {
        assert(strcmp(sess[i].session_id, "w1") != 0);
    }

    /* typed display message still drives the dominant state */
    const char *disp =
        "{\"version\":\"v1\",\"type\":\"display\",\"timestamp\":1400,\"state\":\"error\"}";
    assert(b.onWsMessage(disp, (int)strlen(disp), 1400));
    assert(b.status().state == PET_STATE_ERROR);

    /* snapshot with more sessions than capacity keeps the top 8 */
    char big[2048];
    int pos = snprintf(big, sizeof(big),
                       "{\"version\":\"v1\",\"type\":\"snapshot\",\"timestamp\":1500,"
                       "\"display\":\"idle\",\"sessions\":[");
    for (int i = 0; i < 10; i++) {
        pos += snprintf(big + pos, sizeof(big) - pos,
                        "{\"session_id\":\"s%02d\",\"basename\":\"b\",\"state\":\"idle\","
                        "\"updated_at\":%d,\"priority\":%d},",
                        i, 10 - i, 10 - i);
    }
    pos += snprintf(big + pos, sizeof(big) - pos, "]}");
    assert(b.onWsMessage(big, pos, 1500));
    sess = b.sessions(&n);
    assert(n == PET_BRIDGE_MAX_SESSIONS);

    printf("pet_bridge: all checks passed (%zu callbacks)\n", fired.size());
    return 0;
}
