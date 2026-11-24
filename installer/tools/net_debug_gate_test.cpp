// The rollback layer's test-switch gate (installer/port/jsp/jsp_net_debug_gate.h): a gated key's value applies only with JSP_ROLLBACK_ALLOW_DEBUG=1.
#include "jsp_net_debug_gate.h"
#include <cstdio>
#include <cstring>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main()
{
    using namespace jpdg;
    const char* want[] = { "JSP_NET_LINK", "JSP_AUDIO_TICKLOCK", "JSP_NET_CRI_LOCK", "JSP_NET_GUEST_CLOCK", "JSP_NET_THREAD_REGS", "JSP_ROLLBACK_LIMITED_SAVING", "JSP_ROLLBACK_START",
                           "JSP_ROLLBACK_INPUTS", "JSP_ROLLBACK_INPUTS_SKIP" };
    CHECK(sizeof kGated / sizeof kGated[0] == sizeof want / sizeof want[0]);
    for (const char* k : want) CHECK(is_gated(k));
    CHECK(!is_gated("JSP_ROLLBACK") && !is_gated("JSP_ROLLBACK_LISTEN") && !is_gated("JSP_ROLLBACK_BADGE") && !is_gated(kAllowKey) && !is_gated(nullptr) && !is_gated(""));
    CHECK(!is_gated("JSP_NET_LINKX") && !is_gated("jsp_net_link") && !is_gated("JSP_ROLLBACK_INPUTS_SKIPX") && !is_gated("JSP_ROLLBACK_INPUT"));   // exact names only
    bool ig = true;
    for (const char* k : want) {
        // without the allow switch: a set value is dropped (the default applies) and reported
        ig = false; CHECK(honoured(k, "0", false, &ig) == nullptr && ig);
        ig = false; CHECK(honoured(k, "fight", false, &ig) == nullptr && ig);
        // with it: the value as set
        ig = true; const char* v = honoured(k, "0", true, &ig); CHECK(v && !std::strcmp(v, "0") && !ig);
        // unset or empty: nothing either way, nothing reported
        ig = true; CHECK(honoured(k, "", false, &ig) == nullptr && !ig);
        ig = true; CHECK(honoured(k, nullptr, true, &ig) == nullptr && !ig);
    }
    // the fight hash's A/B switches (jsp_fight_hash.cpp hash_ab): the same rule
    const char* want_hash[] = { "JSP_FIGHT_HASH_RNG_PTRS", "JSP_FIGHT_HASH_SCENE", "JSP_FIGHT_HASH_CAM_CACHE", "JSP_FIGHT_HASH_FIGHTER" };
    CHECK(sizeof kGatedHash / sizeof kGatedHash[0] == sizeof want_hash / sizeof want_hash[0]);
    for (const char* k : want_hash) {
        CHECK(is_gated(k));
        ig = false; CHECK(honoured(k, "1", false, &ig) == nullptr && ig);    // set on one PC without the allow switch: off, reported
        ig = true; const char* v = honoured(k, "1", true, &ig); CHECK(v && v[0] == '1' && !ig);
        ig = true; CHECK(honoured(k, nullptr, false, &ig) == nullptr && !ig);
    }
    CHECK(!is_gated("JSP_FIGHT_HASH") && !is_gated("JSP_FIGHT_HASH_START") && !is_gated("JSP_FIGHT_HASH_SCENEX"));   // the hash itself and its window stay ungated
    // an ungated key passes through (the gate is not a general filter)
    ig = true; CHECK(honoured("JSP_ROLLBACK_BADGE", "1", false, &ig) && !ig);
    CHECK(honoured("JSP_AUDIO_TICKLOCK", "0", false, nullptr) == nullptr);   // null out-pointer is fine
    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("net debug gate: all checks passed\n");
    return 0;
}
