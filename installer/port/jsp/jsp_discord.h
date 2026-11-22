// ENHANCEMENT (Discord Rich Presence and Join/Invite): what the game shows on the player's Discord profile, and Discord's Join / Invite.
// jsp_discord.cpp talks to the local Discord app over its IPC pipe (\\.\pipe\discord-ipc-N); nothing is sent anywhere else. Off unless
// JSP_DISCORD_APP_ID names a Discord application (no built-in one); JSP_DISCORD=0 turns it off. jsp_game.cpp fills JspPresenceRaw from the screens it already
// tracks; the online state comes from the runtime's Matching2 room (patch 7924) and jsp_net_game.cpp (our rollback in the fight).
// The pure parts (presence text, join secret, join rules) are in jsp_discord_state.h.
#pragma once
#include <cstdint>

struct JspPresenceRaw {
    int title = 0;          // the title screen (scene 3 before any fight)
    int menu = 0;           // a menu scene (no fight HUD, scene below the loading steps)
    int charsel = 0;        // character select is up
    int stagesel = 0;       // stage select is up
    int fight = 0;          // the fight HUD was drawn in the last half second
    int training = 0;       // the Training menu row was taken (g_training_mode)
    int stage_focus = -1;   // FIX: the chosen stage (stage select +0x6C), latched when stage select was last up, as its index in the list order of
                            // docs/notes/STAGES.md; -1 = not seen or a stage id not confirmed yet (the presence then shows no stage)
    // The menus' battle setup records (0x009540E4 + 0x2C*slot). FIX: used only for the offline character select's small picture. They
    // are NOT the fight: the native online fight leaves them all zero (= Luffy), and a team is the record's +0x10, not its slot.
    int kind[6] = {0};      // setup slot +0x00 (2 = CPU), slots 0..3 fighters, 4..5 supports
    int part[6] = {0};      // setup slot +0x08 (2 = not taking part)
    int chr[6] = {0};       // setup slot +0x0C (character id: 0..41 fighters, 200..213 supports)
    // FIX (Discord presence, wrong names): the fight's own battle units, read only while the fight HUD is up, in every mode (offline,
    // Training, the native online fight): world W = *(*(TOC-0x7A90)) (func_00030A14), per team a list of 4 unit indices at W+0x34+team*0x18
    // (-1 none), unit records at W+0x60+0x94*index: +0x00 halfword character, +0x08 role (1 leader, 2 partner, 3 Support), +0x0C team,
    // +0x20 3 = a CPU. Only units whose team word matches their list are kept.
    int units = 0;          // units read (0 = no battle world that could be trusted)
    int u_chr[6] = {0}, u_team[6] = {0}, u_role[6] = {0}, u_cpu[6] = {0};
    int menu_row = -1;      // the main menu's cursor 0x00956550 (stays on the entry that was opened: 0 J-Adventure, 3 Free Battle), -1 unknown. FIX
                            // (two-page Main Menu, feat_menu_pages.cpp): every fight starts from the Battle Modes page, whose rows 0..4 keep these
                            // numbers (Training / Impossible Mode open Free Battle's row 3); on the top page 0 is Battle Modes, but no fight starts there
    int ko[2] = {-1, -1};   // KOs scored by red (team 0) / blue (team 1), from the KO counter objects when jsp_game.cpp found them; -1 unknown
};

extern "C" void jsp_game_presence(JspPresenceRaw* out);   // jsp_game.cpp: read-only, any thread
extern "C" int jsp_net_presence(int* players, int* host);  // jsp_net_game.cpp: 1 = an online rollback match is set up (players, host flag)
extern "C" unsigned long long jsp_net_presence_room(char* server, int sn);   // jsp_net_game.cpp: the rollback-only re-psn room (no longer shown)
extern "C" void jsp_discord_start(void);                    // main.cpp: starts the presence thread (not with JSP_DISCORD=0)

// ---- Discord Join into the game's own online room (for the online-menu autopilot) ----
// A friend's Join / an accepted invite arrives as "jsp3|<re-psn server>|<room id>". jsp_discord.cpp checks it (same re-psn server,
// not the room we are in), points the runtime's room search at that room (patch 7924: every SearchRoom answer then lists that one
// room only) and keeps one pending join. The autopilot then drives the game's own menus (Online Battle > Friendly Match > Search),
// picks the one listed room and joins it. Without an autopilot a player who opens that search by hand sees only the friend's room.
//
// jsp_discord_take_join: 1 once per join request (room id, and the re-psn server into server[n]), 0 when none is waiting. Any thread.
extern "C" int jsp_discord_take_join(uint64_t* room, char* server, int n);
// jsp_discord_join_state: where the join is (room = its room id, 0 = none): 0 none, 1 waiting for a room search, 2 the search listed
// the room, 3 joined, -1 the room is gone, -2 it is full, -3 the search's filters left it out (another mode), -4 the join failed.
// FIX: 0 also when the runtime's search is no longer pointed at this join's room (dropped): the autopilot must then stop pressing.
extern "C" int jsp_discord_join_state(uint64_t* room);
// jsp_discord_join_done: the autopilot is finished with the join (ok = 1 joined, 0 gave up): the search shows every room again.
extern "C" void jsp_discord_join_done(int ok);
