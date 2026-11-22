// Host test of installer/port/jsp/jsp_discord_state.h: the Discord join secret, the presence text from a given state, and the join
// rules (stale room, wrong server, full room, Ask to Join). No game, no Discord, no GPU.
#include "../port/jsp/jsp_discord_state.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
using namespace jdp;

static JspPresenceRaw screen(int fight, int charsel = 0)
{
    JspPresenceRaw r;
    r.menu = !fight; r.fight = fight; r.charsel = charsel; r.stage_focus = 6;
    r.menu_row = 3;
    const int chr[6] = {28, 7, 0, 4, 200, 201};
    for (int k = 0; k < 6; k++) { r.chr[k] = chr[k]; r.part[k] = 0; }
    if (fight) {   // the battle units, in the order of the battle world's team lists (team 0: leader, partner, Support; then team 1)
        const int u[6][4] = {{28, 0, 1, 0}, {7, 0, 2, 1}, {200, 0, 3, 1}, {0, 1, 1, 1}, {4, 1, 2, 1}, {201, 1, 3, 1}};
        r.units = 6;
        for (int i = 0; i < 6; i++) { r.u_chr[i] = u[i][0]; r.u_team[i] = u[i][1]; r.u_role[i] = u[i][2]; r.u_cpu[i] = u[i][3]; }
    }
    return r;
}
static Online room(int members, int owner, const char* peer = "")
{
    Online o; o.m2 = 2; o.room = 123456789012ull; o.members = members; o.max = 2; o.owner = owner; o.peer = peer;
    return o;
}

int main()
{
    // ---- the secret ----
    CHECK(encode_secret("repsn.example.org:31313", 42) == "jsp3|repsn.example.org:31313|42");
    CHECK(encode_secret("", 42).empty() && encode_secret("srv", 0).empty() && encode_secret("a|b", 42).empty());
    CHECK(encode_secret(std::string(130, 's'), 42).empty());                       // longer than Discord takes
    CHECK(encode_secret("srv", 18446744073709551615ull) == "jsp3|srv|18446744073709551615");
    JoinReq q;
    CHECK(decode_join("jsp3|repsn.example.org:31313|42", "repsn.example.org:31313", 0, &q) == Verdict::Join && q.room == 42 && q.server == "repsn.example.org:31313");
    CHECK(decode_join(encode_secret("srv", 18446744073709551615ull), "srv", 0, &q) == Verdict::Join && q.room == 18446744073709551615ull);
    CHECK(decode_join("jsp3|REPSN.example.org:31313|42", "repsn.example.org:31313", 0, &q) == Verdict::Join);   // host name case
    CHECK(decode_join("jsp3|other.example.net:31313|42", "repsn.example.org:31313", 0, &q) == Verdict::WrongServer);
    CHECK(decode_join("jsp3|repsn.example.org:31314|42", "repsn.example.org:31313", 0, &q) == Verdict::WrongServer);   // another port
    CHECK(decode_join("jsp3|srv|42", "", 0, &q) == Verdict::NoServer);
    CHECK(decode_join("jsp3|srv|42", "srv", 42, &q) == Verdict::AlreadyThere);
    CHECK(decode_join("jsp3|srv|42", "srv", 41, &q) == Verdict::Join);              // in another room: the autopilot leaves it first
    CHECK(decode_join("jsp2|srv|42", "srv", 0, &q) == Verdict::OldHost);            // the restart-to-join rollback room of older builds
    CHECK(decode_join("jsp1|srv|name", "srv", 0, &q) == Verdict::OldHost);
    CHECK(decode_join("", "srv", 0, &q) == Verdict::NotOurs && decode_join("hello", "srv", 0, &q) == Verdict::NotOurs);
    CHECK(decode_join("jsp3|42", "srv", 0, &q) == Verdict::NotOurs && decode_join("jsp3||42", "srv", 0, &q) == Verdict::NotOurs);
    CHECK(decode_join("jsp3|a|b|42", "a|b", 0, &q) == Verdict::NotOurs);
    CHECK(decode_join("jsp3|srv|0", "srv", 0, &q) == Verdict::BadRoom && decode_join("jsp3|srv|", "srv", 0, &q) == Verdict::BadRoom);
    CHECK(decode_join("jsp3|srv|12a", "srv", 0, &q) == Verdict::BadRoom && decode_join("jsp3|srv|-5", "srv", 0, &q) == Verdict::BadRoom);
    CHECK(decode_join("jsp3|srv|18446744073709551616", "srv", 0, &q) == Verdict::BadRoom);   // one past 2^64-1
    CHECK(decode_join("jsp3|" + std::string(130, 's') + "|1", std::string(130, 's'), 0, &q) == Verdict::NotOurs);

    // ---- presence ----
    Track t;
    JspPresenceRaw menu = screen(0);
    Online off;
    Activity a = build(menu, off, t, "srv");
    CHECK(a.details == "Main menu" && a.state.empty() && a.party_id.empty() && a.join.empty() && a.large == "logo" && a.large_text == "J-Stars Victory VS EX");
    Online m1; m1.m2 = 1;
    note(t, menu, m1); a = build(menu, m1, t, "srv");
    CHECK(a.details == "Online Battle menu" && a.party_id.empty());
    JspPresenceRaw title = menu; title.title = 1;
    CHECK(build(title, m1, t, "srv").details == "Title screen");             // a context left started is not "online" on the title
    Online host = room(1, 1);
    note(t, menu, host); a = build(menu, host, t, "srv");
    CHECK(a.details == "Hosting a Friendly Match" && a.state == "Waiting for an opponent");
    CHECK(a.party_id == "jsp-room-123456789012" && a.size == 1 && a.max == 2 && a.join == "jsp3|srv|123456789012");
    CHECK(build(menu, host, t, "").join.empty());                              // no server: no secret, the party stays
    Online full = room(2, 1, "Rival");
    note(t, menu, full); a = build(menu, full, t, "srv");
    CHECK(a.details == "In a Friendly Match room" && a.state == "with Rival" && a.size == 2 && a.max == 2 && a.join.empty());
    Online guest = room(2, 0, "Hoster");
    CHECK(build(menu, guest, t, "srv").party_id == "jsp-room-123456789012");   // both games: the same party
    JspPresenceRaw sel = screen(0, 1);
    a = build(sel, full, t, "srv");
    CHECK(a.details == "Online character select" && a.state == "vs Rival" && a.small.empty());   // no small picture by default
    JspPresenceRaw fight = screen(1);
    full.rollback = 1;
    note(t, fight, full); a = build(fight, full, t, "srv");
    CHECK(a.details == "Gintoki Sakata & Goku (+Neuro) vs Luffy & Naruto (+Kumagawa)");
    CHECK(a.state == "Online Friendly Match vs Rival (rollback) - Planet Namek" && a.large == "logo" && a.large_text == "J-Stars Victory VS EX" && a.small.empty() && a.join.empty());
    Options art; art.char_art = true;
    CHECK(build(fight, full, t, "srv", art).small.empty());                   // online: which unit is this player's is not known
    full.rollback = 0; full.peer.clear();
    CHECK(build(fight, full, t, "srv").state == "Online Friendly Match - Planet Namek");
    JspPresenceRaw nostage = fight; nostage.stage_focus = -1;                   // a stage not known for sure: no stage name, still the logo
    CHECK(build(fight, full, t, "srv").large == "logo" && build(nostage, full, t, "srv").state == "Online Friendly Match" && build(nostage, full, t, "srv").large == "logo");
    full.peer = "Rival";
    note(t, menu, full); a = build(menu, full, t, "srv");
    CHECK(a.details == "After the fight" && a.state == "vs Rival");
    CHECK(build(sel, full, t, "srv").details == "Online character select");    // Re-select characters
    Online other = room(2, 1, "Rival"); other.room = 7;                         // a new room: no fight seen there yet
    note(t, menu, other); CHECK(build(menu, other, t, "srv").details == "In a Friendly Match room");
    note(t, menu, off); CHECK(t.room == 0 && !t.fought);
    a = build(fight, off, t, "srv");
    CHECK(a.state == "Free Battle - Planet Namek" && a.party_id.empty());
    Online rb; rb.rollback = 1;
    CHECK(build(fight, rb, t, "srv").state == "Online (rollback) - Planet Namek");
    Online joining; joining.m2 = 1; joining.joining = 1;
    CHECK(build(menu, joining, t, "srv").details == "Joining a friend's room");
    Online longname = room(2, 1, ""); longname.peer = std::string(200, 'n');
    CHECK(build(sel, longname, t, "srv").state.size() == kTextMax);

    // ---- names, the fight's units, the 128-character limit ----
    CHECK(std::string(char_name(0)) == "Luffy" && std::string(char_name(28)) == "Gintoki Sakata" && std::string(char_name(41)) == "Nube");
    CHECK(!char_name(-1) && !char_name(42) && !char_name(199) && !char_name(200));
    CHECK(std::string(support_name(200)) == "Neuro" && std::string(support_name(213)) == "Hinata" && !support_name(199) && !support_name(214));
    CHECK(std::string(unit_name(7)) == "Goku" && std::string(unit_name(202)) == "Rukia" && !unit_name(100) && !unit_name(0x7FFF));
    {
        JspPresenceRaw f = screen(1);
        CHECK(matchup(f) == "Gintoki Sakata & Goku (+Neuro) vs Luffy & Naruto (+Kumagawa)");
        JspPresenceRaw zero = f;                                                // the menus' setup records play no part in the fight
        for (int k = 0; k < 6; k++) zero.chr[k] = 0;
        CHECK(build(zero, off, t, "srv").details == "Gintoki Sakata & Goku (+Neuro) vs Luffy & Naruto (+Kumagawa)");
        JspPresenceRaw rev = f;                                                 // partner listed before the leader: the leader still first
        std::swap(rev.u_chr[0], rev.u_chr[1]); std::swap(rev.u_role[0], rev.u_role[1]); std::swap(rev.u_cpu[0], rev.u_cpu[1]);
        CHECK(team_text(rev, 0, false) == "Gintoki Sakata & Goku");
        JspPresenceRaw one = f; one.units = 4;                                  // 1 v 1: team 0 Gintoki + Support Edajima, team 1 Luffy alone
        one.u_role[1] = 3; one.u_chr[1] = 205;
        CHECK(matchup(one) == "Gintoki Sakata (+Edajima) vs Luffy");
        JspPresenceRaw bad = f; bad.u_chr[3] = 0x7FFF; bad.u_chr[4] = -3;       // team 1's fighters unknown: no names at all, never junk
        CHECK(matchup(bad).empty() && build(bad, off, t, "srv").details == "In battle");
        JspPresenceRaw badsup = f; badsup.u_chr[2] = 9999;                      // an unknown Support is just left out
        CHECK(matchup(badsup) == "Gintoki Sakata & Goku vs Luffy & Naruto (+Kumagawa)");
        JspPresenceRaw sup_as_fighter = f; sup_as_fighter.u_chr[1] = 204;       // a Support id in a fighter slot is not shown as a fighter
        CHECK(team_text(sup_as_fighter, 0, false) == "Gintoki Sakata");
        JspPresenceRaw none = f; none.units = 0;
        CHECK(build(none, off, t, "srv").details == "In battle");
        JspPresenceRaw garbage = f; garbage.units = 99;                         // a count out of range reads 6 at most
        CHECK(!matchup(garbage).empty());
        JspPresenceRaw lng = f;                                                 // the longest names: everything still within 128
        const int ids[6] = {28, 31, 206, 36, 27, 206};
        for (int i = 0; i < 6; i++) lng.u_chr[i] = ids[i];
        const std::string m = matchup(lng);
        CHECK(m.size() <= kTextMax && m == "Gintoki Sakata & Arale & Gatchan (+Kagura & Sadaharu) vs Tsuna & Reborn & Oga & Baby Beel (+Kagura & Sadaharu)");
        size_t worst = 0;                                                       // every pair of fighters, both teams
        for (int x = 0; x < 42; x++) for (int y = 0; y < 42; y++) {
            JspPresenceRaw w = lng; w.u_chr[0] = w.u_chr[3] = x; w.u_chr[1] = w.u_chr[4] = y;
            const Activity aw = build(w, longname, t, "srv");
            worst = std::max(worst, aw.details.size());
            if (aw.details.size() > kTextMax || aw.state.size() > kTextMax) fails++;
        }
        CHECK(worst <= kTextMax && worst > 60);
        // modes
        JspPresenceRaw tr = f; tr.training = 1;
        CHECK(build(tr, off, t, "srv").state == "Training - Planet Namek");
        JspPresenceRaw ja = f; ja.menu_row = 0; ja.stage_focus = -1;
        CHECK(build(ja, off, t, "srv").state == "J-Adventure");
        JspPresenceRaw other_row = f; other_row.menu_row = 2; other_row.stage_focus = -1;
        CHECK(build(other_row, off, t, "srv").state == "Offline battle");
        JspPresenceRaw cs = screen(0, 1);
        CHECK(build(cs, off, t, "srv").details == "Character select" && build(cs, off, t, "srv").state == "Free Battle");
        // KOs
        JspPresenceRaw k = f; k.ko[0] = 2; k.ko[1] = 1;
        CHECK(build(k, off, t, "srv").state == "Free Battle - 2-1 KOs - Planet Namek");
        k.ko[1] = -1; CHECK(build(k, off, t, "srv").state == "Free Battle - Planet Namek");   // one side unknown: no score
        k.ko[1] = 999; CHECK(build(k, off, t, "srv").state == "Free Battle - Planet Namek");  // garbage: no score
        // a long online name: the state stays within 128, the extras give way
        Online lp = room(2, 1, ""); lp.peer = std::string(120, 'p'); lp.rollback = 1;
        JspPresenceRaw kk = f; kk.ko[0] = 1; kk.ko[1] = 0;
        const Activity al = build(kk, lp, t, "srv");
        CHECK(al.state.size() == kTextMax && al.state.find("KOs") == std::string::npos);
        CHECK(redact(al.state, lp.peer).find("pppp") == std::string::npos);   // the log never shows the name, even cut
        // char art (JSP_DISCORD_CHAR_ART=1): offline only, red's human leader
        CHECK(!char_art_setting("") && !char_art_setting("0") && char_art_setting("1"));
        Options art2; art2.char_art = true;
        const Activity ac = build(f, off, t, "srv", art2);
        CHECK(ac.small == "char_028" && ac.small_text == "Gintoki Sakata");
        CHECK(build(f, off, t, "srv").small.empty());                           // off by default
        JspPresenceRaw cpu = f; cpu.u_cpu[0] = 1;                               // no human leader on red: red's leader anyway
        CHECK(build(cpu, off, t, "srv", art2).small == "char_028");
        CHECK(build(cs, off, t, "srv", art2).small == "char_028");              // character select: player 1's pick
        JspPresenceRaw cs_bad = cs; cs_bad.chr[0] = 500;
        CHECK(build(cs_bad, off, t, "srv", art2).small.empty());
        CHECK(build(menu, off, t, "srv", art2).small.empty());
    }
    // ---- redaction, UTF-8 cut ----
    CHECK(redact("Online Friendly Match vs Rival (rollback)", "Rival") == "Online Friendly Match vs <opponent> (rollback)");
    CHECK(redact("with Rival", "") == "with Rival" && redact("Main menu", "Rival") == "Main menu");
    {
        const std::string cut = std::string(kTextMax - 3, 'x') + "Riv";      // the name cut at the limit
        CHECK(redact(cut, "Rival").find("Riv") == std::string::npos);
        const std::string u8 = std::string(kTextMax - 1, 'a') + "\xC3\xA9";   // a 2-byte character across the limit
        CHECK(clip(u8).size() == kTextMax - 1);
    }
    // ---- the fight's timer ----
    {
        FightClock c;
        CHECK(fight_start(c, false, 0, 1000, 100) == 0);
        CHECK(fight_start(c, true, 7, 2000, 101) == 101);                      // the fight starts
        CHECK(fight_start(c, true, 7, 4000, 103) == 101);
        CHECK(fight_start(c, false, 7, 6000, 105) == 0);                       // a cut-in hides the HUD for a moment
        CHECK(fight_start(c, true, 7, 8000, 107) == 101);                      // ... the same fight
        CHECK(fight_start(c, true, 0, 9000, 108) == 101);                      // the units not readable for one update: same fight
        CHECK(fight_start(c, false, 0, 10000, 109) == 0 && fight_start(c, false, 0, 30000, 129) == 0);   // back to the menus
        CHECK(fight_start(c, true, 7, 32000, 131) == 131);                     // a rematch with the same line-up: a new fight
        CHECK(fight_start(c, true, 9, 34000, 133) == 133);                     // another line-up at once: a new fight
        CHECK(lineup_sig(screen(1)) != 0 && lineup_sig(screen(0)) == 0);
        FightClock h; fight_start(h, true, 7, 50000, 500);                      // the HUD gone for a moment: the fight is still shown
        JspPresenceRaw gone = screen(0); gone.menu = 0;
        CHECK(hold_fight(h, gone, 52000) && hold_fight(h, gone, 55999) && !hold_fight(h, gone, 56000));
        JspPresenceRaw sel2 = screen(0, 1); CHECK(!hold_fight(h, sel2, 52000));   // a select screen: shown at once
        CHECK(!hold_fight(FightClock(), gone, 52000));                          // no fight seen
        JspPresenceRaw o2 = screen(1); o2.u_chr[0] = 7;
        CHECK(lineup_sig(o2) != lineup_sig(screen(1)));
        CHECK(build(screen(1), off, t, "srv", Options(), 131).start == 131 && build(menu, off, t, "srv", Options(), 131).start == 0);
    }

    // ---- Ask to Join on the host ----
    CHECK(auto_accept(true, room(1, 1)) && !auto_accept(false, room(1, 1)));
    CHECK(!auto_accept(true, room(2, 1)));                                     // full: Discord's own prompt
    CHECK(!auto_accept(true, off) && !auto_accept(true, m1));
    CHECK(!auto_accept_setting("") && !auto_accept_setting("0") && auto_accept_setting("1"));   // JSP_DISCORD_AUTO_ACCEPT: off unless =1

    // ---- the pending join ----
    JoinSlot s;
    JoinReq r; r.room = 42; r.server = "srv";
    CHECK(!s.active() && !s.take(nullptr) && s.step(kJoinNone, 5) == Step::Idle);
    s.offer(r, 1000);
    JoinReq got;
    CHECK(s.take(&got) && got.room == 42 && got.server == "srv" && !s.take(&got));   // once per request
    CHECK(s.step(kJoinPending, 2000) == Step::Wait && s.step(kJoinListed, 5000) == Step::Wait);
    CHECK(s.step(kJoinJoined, 6000) == Step::Wait && s.step(kJoinJoined, 20999) == Step::Wait && s.step(kJoinJoined, 21000) == Step::Done);
    s.offer(r, 1000);                                                           // a stale room: the search did not list it
    CHECK(s.step(kJoinGone, 2000) == Step::Wait && s.step(kJoinGone, 21999) == Step::Wait && s.step(kJoinGone, 22000) == Step::GiveUp);
    s.offer(r, 1000);                                                           // a full room
    CHECK(s.step(kJoinFull, 20999) == Step::Wait && s.step(kJoinFull, 40998) == Step::Wait && s.step(kJoinFull, 40999) == Step::GiveUp);
    s.offer(r, 1000);                                                           // the grace runs from the FIRST failure, not from the offer
    CHECK(s.step(kJoinPending, 60000) == Step::Wait && s.step(kJoinGone, 90000) == Step::Wait && s.failed_at == 90000);
    CHECK(s.step(kJoinGone, 109999) == Step::Wait && s.step(kJoinGone, 110000) == Step::GiveUp);
    s.offer(r, 1000);                                                           // a failure that clears (searched again) restarts the grace
    CHECK(s.step(kJoinGone, 2000) == Step::Wait && s.step(kJoinPending, 15000) == Step::Wait && s.failed_at == 0);
    CHECK(s.step(kJoinFiltered, 16000) == Step::Wait && s.step(kJoinFiltered, 35999) == Step::Wait && s.step(kJoinFiltered, 36000) == Step::GiveUp);
    s.offer(r, 1000);                                                           // nobody opens the online menus
    CHECK(s.step(kJoinPending, 180999) == Step::Wait && s.step(kJoinPending, 181000) == Step::GiveUp);
    s.offer(r, 1000);                                                           // joined, then left again before it settled
    CHECK(s.step(kJoinJoined, 2000) == Step::Wait && s.step(kJoinListed, 3000) == Step::Wait && s.joined_at == 0);
    s.offer(r, 1000); CHECK(s.take(nullptr));
    JoinReq r2; r2.room = 43; r2.server = "srv"; s.offer(r2, 2000);             // a newer request replaces the old one and is taken again
    CHECK(s.take(&got) && got.room == 43);
    s.clear(); CHECK(!s.active() && s.step(kJoinJoined, 99999) == Step::Idle);
    CHECK(std::string(join_state_text(kJoinFull)) == "the room is full" && std::string(verdict_text(Verdict::WrongServer)).find("server") != std::string::npos);
    // FIX (review): no built-in application: unset is off, only a numeric id of up to 20 digits is used
    CHECK(app_id_setting("") == AppId::Unset && app_id_setting("123456789012345678") == AppId::Ok && app_id_setting("18446744073709551615") == AppId::Ok);
    CHECK(app_id_setting("12a") == AppId::Invalid && app_id_setting(" 1") == AppId::Invalid && app_id_setting("123456789012345678901") == AppId::Invalid);

    if (fails) { std::printf("discord: %d checks FAILED\n", fails); return 1; }
    std::printf("PASS discord: secret, presence text, names, timer and join rules\n");
    return 0;
}
