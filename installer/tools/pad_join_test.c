/* Host test of libs/input/pad_join.h (toolkit patch 7908): the device-to-player table of cellPad. No game, no pad, no GPU.
 *   gcc -O1 -Wall -I<patched tree>/libs/input installer/tools/pad_join_test.c -o pad_join_test
 * Devices: 0..3 host pad slots, 4 keyboard. Players 0..3. */
#include "pad_join.h"
#include <stdio.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

#define CROSS 0x4000
#define CIRCLE 0x2000
static PadJoin j;
static PadJoinDev in[PJ_DEVS];

static void reset_devs(void) { memset(in, 0, sizeof in); }
static void connect(int d, int must) { in[d].connected = 1; in[d].must = (uint8_t)must; in[d].buttons = 0; }
static void upd(void) { pj_update(&j, in); }
static void press(int d, uint16_t b) { in[d].buttons = b; upd(); }
static void release_all(void) { for (int d = 0; d < PJ_DEVS; d++) in[d].buttons = 0; upd(); }
static void start_join(void) { pj_begin(&j); upd(); }
/* the game's view of every player's device: -1 none, else the device */
static int dev(int p) { return pj_dev_for_player(&j, p); }

int main(void)
{
    /* no reorder: the table is not in force and holds nothing */
    pj_init(&j); reset_devs();
    connect(0, 1); connect(1, 1);
    press(0, CROSS);
    CHECK(!j.active && !j.joining && dev(0) == -1 && dev(1) == -1 && pj_next_player(&j) == 0);
    for (int d = 0; d < PJ_DEVS; d++) CHECK(!pj_hidden(&j, d));

    /* join order is the press order, not the slot order; the presses are swallowed */
    pj_init(&j); reset_devs();
    connect(0, 1); connect(1, 1); connect(2, 1);
    start_join();
    CHECK(j.active && j.joining && pj_next_player(&j) == 1 && dev(0) == -1);
    press(2, CROSS);                                   /* slot 2 first */
    CHECK(dev(0) == 2 && pj_hidden(&j, 2) && j.joining && pj_next_player(&j) == 2);
    press(2, CROSS);                                   /* still held: no second join, still hidden */
    CHECK(dev(1) == -1 && pj_hidden(&j, 2));
    release_all();
    CHECK(!pj_hidden(&j, 2));                          /* visible again after release */
    press(0, CIRCLE);
    CHECK(dev(1) == 0 && pj_hidden(&j, 0) && j.joining);
    release_all();
    CHECK(dev(0) == 2 && dev(1) == 0 && dev(2) == -1 && j.joining);   /* slot 1 has not pressed: not given */
    press(1, CROSS);
    CHECK(dev(2) == 1 && !j.joining && pj_hidden(&j, 1));   /* all joined: join ends, press still swallowed */
    release_all();
    CHECK(!pj_hidden(&j, 1) && dev(0) == 2 && dev(1) == 0 && dev(2) == 1 && dev(3) == -1);
    /* after join the order stays */
    press(0, CROSS); release_all();
    CHECK(dev(0) == 2 && dev(1) == 0 && dev(2) == 1);

    /* two devices pressing in one poll: device order breaks the tie */
    pj_init(&j); reset_devs(); connect(1, 1); connect(3, 1);
    start_join(); release_all();
    in[1].buttons = CROSS; in[3].buttons = CROSS; upd();
    CHECK(dev(0) == 1 && dev(1) == 3 && !j.joining);

    /* the press that started join (still held) does not join; a release comes first */
    pj_init(&j); reset_devs(); connect(0, 1); connect(1, 1);
    in[0].buttons = CROSS;                              /* menu row: Cross held while join begins */
    start_join();
    upd();
    CHECK(dev(0) == -1 && j.joining);
    release_all();
    press(1, CROSS);
    CHECK(dev(0) == 1);

    /* Start from player 1 ends join; devices that never pressed take the next free players; the Start is swallowed */
    pj_init(&j); reset_devs(); connect(0, 1); connect(1, 1); connect(2, 1);
    start_join(); release_all();
    press(2, CROSS); release_all();                     /* slot 2 is player 1 */
    press(0, PJ_BTN_START);                             /* a joining press (Start is just a button for a device that has none) */
    CHECK(dev(1) == 0 && j.joining); release_all();
    press(2, PJ_BTN_START);                             /* Start from player 1 */
    CHECK(!j.joining && dev(0) == 2 && dev(1) == 0 && dev(2) == 1 && pj_hidden(&j, 2));
    release_all();
    CHECK(!pj_hidden(&j, 2));

    /* a Start from the device that has just joined as player 1 is its join press, not "end join" */
    pj_init(&j); reset_devs(); connect(0, 1); connect(1, 1);
    start_join(); release_all();
    press(0, PJ_BTN_START);
    CHECK(dev(0) == 0 && j.joining);

    /* disconnect frees the player; a reconnect takes the lowest free player */
    pj_init(&j); reset_devs(); connect(0, 1); connect(1, 1); connect(2, 1);
    start_join(); release_all();
    press(0, CROSS); release_all(); press(1, CROSS); release_all(); press(2, CROSS); release_all();
    CHECK(!j.joining && dev(0) == 0 && dev(1) == 1 && dev(2) == 2);
    in[1].connected = 0; upd();
    CHECK(dev(0) == 0 && dev(1) == -1 && dev(2) == 2);
    connect(3, 1); upd();                               /* a new device: lowest free = player 2 */
    CHECK(dev(1) == 3 && dev(3) == -1);
    in[3].connected = 0; connect(1, 1); upd();
    CHECK(dev(1) == 1 && dev(2) == 2);

    /* player 1 stays occupied whenever any device has a player */
    in[0].connected = 0; upd();
    CHECK(dev(0) == 1 && dev(1) == -1 && dev(2) == 2);   /* the lowest other player moved up */
    in[1].connected = 0; upd();
    CHECK(dev(0) == 2 && dev(1) == -1);
    in[2].connected = 0; upd();
    CHECK(dev(0) == -1 && dev(1) == -1 && dev(2) == -1 && dev(3) == -1);   /* nobody: cellPad serves its neutral port 0 */
    connect(0, 1); upd();
    CHECK(dev(0) == 0);

    /* a device that disconnects during join leaves no player behind; the rest go on */
    pj_init(&j); reset_devs(); connect(0, 1); connect(1, 1); connect(2, 1);
    start_join(); release_all();
    press(1, CROSS); release_all(); press(2, CROSS); release_all();
    CHECK(dev(0) == 1 && dev(1) == 2 && j.joining);
    in[1].connected = 0; upd();                         /* player 1 leaves mid-join: player 2 moves up */
    CHECK(dev(0) == 2 && dev(1) == -1 && j.joining);
    in[0].connected = 0; upd();                         /* the one that never pressed leaves: nobody waits any more */
    CHECK(!j.joining && dev(0) == 2);

    /* the keyboard is a device; where it is not `must` it can join but never holds up the end */
    pj_init(&j); reset_devs(); connect(0, 1); connect(1, 1); connect(PJ_DEV_KEYBOARD, 0);
    start_join(); release_all();
    press(0, CROSS); release_all(); press(1, CROSS);
    CHECK(!j.joining && dev(0) == 0 && dev(1) == 1 && dev(2) == PJ_DEV_KEYBOARD);   /* keyboard never pressed: takes the next free player when join ends */
    CHECK(!pj_hidden(&j, PJ_DEV_KEYBOARD));
    pj_init(&j); reset_devs(); connect(0, 1); connect(PJ_DEV_KEYBOARD, 0);
    start_join(); release_all();
    press(PJ_DEV_KEYBOARD, CROSS); release_all();       /* a key first: the keyboard is player 1 */
    CHECK(dev(0) == PJ_DEV_KEYBOARD && j.joining);
    press(0, CROSS);
    CHECK(dev(1) == 0 && !j.joining);
    /* keyboard that must join (INPUT_MODE keyboard / kbm) holds join open */
    pj_init(&j); reset_devs(); connect(0, 1); connect(PJ_DEV_KEYBOARD, 1);
    start_join(); release_all();
    press(0, CROSS); release_all();
    CHECK(j.joining && dev(0) == 0);
    press(PJ_DEV_KEYBOARD, CROSS);
    CHECK(!j.joining && dev(1) == PJ_DEV_KEYBOARD);

    /* four devices fill four players; a fifth stays out; the table ends at once when full */
    pj_init(&j); reset_devs();
    for (int d = 0; d < PJ_DEVS; d++) connect(d, 1);
    start_join(); release_all();
    const int order[5] = { 3, 1, 4, 0, 2 };
    for (int k = 0; k < 4; k++) { CHECK(j.joining && pj_next_player(&j) == k + 1); press(order[k], CROSS); release_all(); }
    CHECK(!j.joining && dev(0) == 3 && dev(1) == 1 && dev(2) == 4 && dev(3) == 0);
    CHECK(j.player_of[2] == -1);                        /* the fifth device has no player */
    in[1].connected = 0; upd();                         /* a place frees up: the fifth takes it */
    CHECK(dev(1) == 2 && dev(0) == 3);

    /* redo the order without unplugging: a second begin clears the table */
    pj_begin(&j); upd();
    CHECK(j.joining && dev(0) == -1 && dev(1) == -1 && pj_next_player(&j) == 1);
    release_all();
    press(0, CROSS);
    CHECK(dev(0) == 0 && j.joining);

    /* player 1 always connected: after every update of a random walk, if any device has a player, player 1 has one */
    pj_init(&j); reset_devs();
    unsigned seed = 12345;
    start_join();
    for (int step = 0; step < 20000; step++) {
        seed = seed * 1103515245u + 12345u;
        const int d = (int)((seed >> 16) % PJ_DEVS), act = (int)((seed >> 8) % 4);
        if (act == 0) { in[d].connected = !in[d].connected; in[d].must = (uint8_t)(d != 4); }
        else if (act == 1) in[d].buttons = (uint16_t)(1u << ((seed >> 3) % 15));
        else if (act == 2) in[d].buttons = 0;
        else if ((seed >> 20) % 50 == 0) pj_begin(&j);
        upd();
        int any = 0;
        for (int p = 0; p < PJ_PLAYERS; p++) any |= dev(p) >= 0;
        if (any && dev(0) < 0) { CHECK(0 && "player 1 empty while another player exists"); break; }
        for (int p = 0; p < PJ_PLAYERS; p++)
            if (dev(p) >= 0) { if (!in[dev(p)].connected || j.player_of[dev(p)] != p) { CHECK(0 && "table inconsistent"); step = 1 << 30; break; } }
    }

    /* chord: Select + L3 + R3 held PJ_CHORD_MS on one device, once per hold */
    pj_init(&j); reset_devs(); connect(0, 1);
    in[0].buttons = PJ_CHORD_BITS;
    CHECK(!pj_chord(&j, in, 1000) && pj_chord_held(&j, 0));
    CHECK(!pj_chord(&j, in, 1000 + PJ_CHORD_MS - 1));
    CHECK(pj_chord(&j, in, 1000 + PJ_CHORD_MS));
    CHECK(!pj_chord(&j, in, 1000 + PJ_CHORD_MS + 500));      /* not again while held */
    in[0].buttons = PJ_BTN_SELECT | PJ_BTN_L3;                /* one released: reset */
    CHECK(!pj_chord(&j, in, 4000) && !pj_chord_held(&j, 0));
    in[0].buttons = PJ_BTN_START | PJ_BTN_SELECT;             /* Select + Start is not the chord (Start is the game's pause) */
    CHECK(!pj_chord(&j, in, 5000) && !pj_chord(&j, in, 9000));
    in[0].buttons = PJ_CHORD_BITS; in[0].connected = 0;       /* a disconnected device never fires */
    CHECK(!pj_chord(&j, in, 10000) && !pj_chord(&j, in, 20000));


    /* ---- round 2: the view the game keeps during join, P8 held devices, chord reset ---- */
    /* first join (no table before): the game keeps its default layout (pj_view_default), no device is viewed through the table */
    pj_init(&j); reset_devs(); connect(0, 1); connect(1, 1);
    CHECK(!pj_view_default(&j));                        /* no reorder yet: the caller does not use the table at all */
    start_join();
    CHECK(pj_view_default(&j) && pj_view_dev(&j, 0) == -1);
    release_all(); press(1, CROSS); release_all(); press(0, CROSS);
    CHECK(!j.joining && !pj_view_default(&j) && pj_view_dev(&j, 0) == 1 && pj_view_dev(&j, 1) == 0);   /* one step at the end */

    /* redo: the previous table stays what the game sees until join ends; a leaver drops out of it; player 1 moves up there too */
    CHECK(dev(0) == 1 && dev(1) == 0);
    pj_begin(&j); upd();
    CHECK(j.joining && !pj_view_default(&j) && pj_view_dev(&j, 0) == 1 && pj_view_dev(&j, 1) == 0 && dev(0) == -1);
    release_all(); press(0, CROSS); release_all();      /* new table: slot 0 first */
    CHECK(pj_view_dev(&j, 0) == 1 && pj_view_dev(&j, 1) == 0 && dev(0) == 0);   /* still the old view */
    in[1].connected = 0; upd();
    CHECK(pj_view_dev(&j, 0) == 0 && pj_view_dev(&j, 1) == -1);               /* device 1 left: device 0 moved up in the view */
    connect(1, 1); upd(); release_all(); press(1, CROSS);
    CHECK(!j.joining && pj_view_dev(&j, 0) == 0 && pj_view_dev(&j, 1) == 1);

    /* P8: a device held since before join is not auto-assigned showing its buttons */
    pj_init(&j); reset_devs(); connect(0, 1); connect(1, 1);
    in[0].buttons = CROSS; upd();
    start_join(); upd();
    press(1, PJ_BTN_START);                             /* joins as player 1 with Start (not an end) */
    CHECK(dev(0) == 1 && j.joining);
    release_all();
    CHECK(dev(0) == 1 && j.armed[0]);
    in[0].buttons = CROSS; upd(); in[0].buttons = 0; upd();
    pj_init(&j); reset_devs(); connect(0, 1); connect(1, 1);
    start_join(); release_all();
    in[0].buttons = CROSS; upd(); release_all();        /* player 1 = device 0 */
    in[1].buttons = CIRCLE;                             /* device 1 never pressed; it is holding a button when the next join begins */
    pj_begin(&j); upd();
    in[1].buttons = CIRCLE; upd();                      /* held, not armed: cannot join */
    CHECK(dev(0) == -1 && dev(1) == -1);
    in[0].buttons = 0; upd(); in[0].buttons = CROSS; upd();   /* device 0 released and pressed: player 1 */
    CHECK(dev(0) == 0 && j.joining);
    in[0].buttons = 0; upd(); in[0].buttons = PJ_BTN_START; upd();   /* player 1 Start ends join; device 1 is still held */
    CHECK(!j.joining && dev(1) == 1 && pj_hidden(&j, 1));          /* given a place but neutral */
    in[1].buttons = 0; upd();
    CHECK(!pj_hidden(&j, 1));
    /* plugged in with a button down after join: place, but neutral until released (disconnect + reconnect while held) */
    in[1].connected = 0; upd();
    CHECK(dev(1) == -1);
    in[1].connected = 1; in[1].buttons = CROSS; upd();
    CHECK(dev(1) == 1 && pj_hidden(&j, 1));
    in[1].buttons = 0; upd();
    CHECK(!pj_hidden(&j, 1));

    /* P4: the chord device's Select / L3 / R3 are not hidden during or after join, and the chord does not refire while still held */
    pj_init(&j); reset_devs(); connect(0, 1); connect(1, 1);
    in[0].buttons = PJ_CHORD_BITS;
    CHECK(!pj_chord(&j, in, 1000) && pj_chord_held(&j, 0));
    CHECK(pj_chord(&j, in, 1000 + PJ_CHORD_MS));
    pj_begin(&j); pj_update(&j, in);
    CHECK(j.joining && !pj_chord_held(&j, 0) && j.chord_since[0] == 0);
    in[0].buttons = 0; pj_update(&j, in);
    in[0].buttons = PJ_BTN_SELECT; pj_update(&j, in);    /* Select during join is just a joining press, swallowed as one */
    CHECK(dev(0) == 0 && pj_hidden(&j, 0));
    in[0].buttons = PJ_CHORD_BITS; pj_update(&j, in);
    in[1].buttons = 0; pj_update(&j, in); in[1].buttons = CROSS; pj_update(&j, in);   /* join ends by device 1 */
    CHECK(!j.joining && !pj_chord_held(&j, 0));
    CHECK(!pj_chord(&j, in, 50000) && !pj_chord(&j, in, 90000) && !pj_chord_held(&j, 0));   /* held through the end: release first */
    in[0].buttons = 0;
    CHECK(!pj_chord(&j, in, 100000));
    in[0].buttons = PJ_CHORD_BITS;
    CHECK(!pj_chord(&j, in, 101000) && pj_chord(&j, in, 101000 + PJ_CHORD_MS));

    /* adversarial cases: Start by a device that is not player 1 is a plain press; five devices pressing at once in keyboard mode */
    pj_init(&j); reset_devs(); for (int d = 0; d < 3; d++) connect(d, 1);
    start_join(); release_all();
    press(2, PJ_BTN_START);
    CHECK(dev(0) == 2 && j.joining);
    release_all(); press(1, PJ_BTN_START);
    CHECK(dev(1) == 1 && j.joining);                    /* not player 1: no end */
    pj_init(&j); reset_devs(); for (int d = 0; d < PJ_DEVS; d++) connect(d, 1);
    start_join(); release_all();
    for (int d = 0; d < PJ_DEVS; d++) in[d].buttons = CROSS;
    upd();
    CHECK(!j.joining && dev(0) == 0 && dev(1) == 1 && dev(2) == 2 && dev(3) == 3 && j.player_of[4] == -1);
    release_all();
    in[2].connected = 0; upd();
    CHECK(dev(2) == PJ_DEV_KEYBOARD);                   /* the fifth takes the freed place */

    /* ---- round 3: the chord bits never reach the game, from the first poll of the hold to the release ---- */
    {
        /* the order cellPad.c's stage works in: chord (when not joining), update, then what the game may see */
        uint64_t t = 1000; int leaked = 0;
        for (int round = 0; round < 2; round++) {         /* round 0: first join; round 1: a redo from player 1's device */
            if (round == 0) { pj_init(&j); reset_devs(); connect(0, 1); connect(1, 1); }
            const int holder = round == 0 ? 0 : j.dev_of[0];
            in[holder].buttons = PJ_CHORD_BITS;
            int fired = 0;
            for (int i = 0; i < 160; i++) {                /* 2.5 s at 16 ms */
                t += 16;
                PadJoinDev p[PJ_DEVS]; memcpy(p, in, sizeof p); p[PJ_DEV_KEYBOARD].connected = 0;
                if (!j.joining && pj_chord(&j, p, t)) { pj_begin(&j); fired = 1; }
                if (j.active || j.request) pj_update(&j, in);
                if (pj_chord_hide(&j, holder, in[holder].buttons) & PJ_CHORD_BITS) leaked++;
                if (pj_chord_hide(&j, holder, in[holder].buttons & ~PJ_BTN_L3) & PJ_CHORD_BITS) leaked++;   /* one bit let go: the rest stays hidden */
                if (round == 1 && fired) CHECK(pj_view_dev(&j, 0) == holder);   /* the kept view: the holder still sits there */
            }
            CHECK(fired && j.joining);
            in[holder].buttons = 0; t += 16; pj_update(&j, in);
            CHECK(pj_chord_hide(&j, holder, PJ_BTN_SELECT) == PJ_BTN_SELECT);   /* released: nothing hidden any more */
            release_all();
            if (round == 0) { press(1, CROSS); release_all(); press(0, CROSS); release_all(); CHECK(!j.joining); }
        }
        CHECK(leaked == 0);
    }

    printf(fails ? "pad_join_test: %d FAILED\n" : "pad_join_test: all passed\n", fails);
    return fails != 0;
}
