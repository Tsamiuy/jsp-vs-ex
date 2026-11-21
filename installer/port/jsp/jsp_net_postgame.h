// jsp_net_postgame.h -- ENHANCEMENT (experimental rollback netcode): the post-game choice after an online rollback match.
// Both players pick Rematch (same characters, supports, stage), Change characters (both back to character select) or Leave (back to the
// online lobby; the other player is told "Opponent left"). Header only, no Windows or game calls, so installer/tools/net_postgame_test.cpp
// runs it on the host. The choice is made on the game's own after-fight menu (NativeGate at the bottom: Fight again / Re-select
// character(s) / Return to main menu); the net code carries the messages over the existing link and acts on the outcome.
//
// The agreement (each side runs one Agreement; every message is the sender's whole state, so only the newest one matters):
//   - mine / peer: NONE, REMATCH, CHARSELECT or LEAVE. A side may change its pick until it has seen a pick of the peer; from then on
//     its pick is locked (final). Its messages say so (flag "saw": I have seen a pick of yours).
//   - LEAVE decides at once: the side that picks it leaves, the side that receives it leaves with "Opponent left".
//   - Otherwise a side decides when its own pick is locked AND the peer's newest message says "saw" (the peer's pick is locked too).
//     The outcome is the larger of the two final picks: REMATCH only when both picked REMATCH, else CHARSELECT. Both sides take the
//     same two final picks, so both decide the same, whatever the order, loss or duplication of messages (net_postgame_test.cpp checks
//     every interleaving of small scripts and random reordered, duplicated and stale traffic).
//   - Why it holds: a side's message with "saw" set carries its final pick (it was locked before it set "saw"). A side decides only on
//     such a message and on its own locked pick. Its own pick was locked when it saw the peer's pick, and the peer can only have
//     decided on a "saw" message of this side, which carries the locked pick.
//   - rev: every change of a side's state (pick or "saw") raises its revision; a message with a revision not above the last one taken
//     from that side is a duplicate or arrived late and changes nothing (it still proves the link is alive).
//   - seq: the match number the net code gives both sides (the same on both PCs: count the rollback matches of the link, from 1). A
//     message for another seq is a stale one of an earlier match, or an early one of the next match (the peer reached its post-game
//     first): kept aside (newest per seq, a few seqs) and taken when start() is called with that seq; a stale one is dropped there.
//   - Timeouts: no message from the peer for silence_ms (kSilenceMs, short: a dead connection) -> Leave, "Connection lost". The local
//     player has not locked a pick within idle_ms (kIdleMs = 3 minutes; the net code may take JSP_POSTGAME_IDLE_SECS) -> the side
//     picks LEAVE itself (sent like a pick: the peer sees "Opponent left"). The view shows the time left only in the last kCountdownMs.
//     Locked but not decided after idle_ms + silence_ms -> Leave, "Connection lost" (the peer keeps resending, so this is a dead link).
//
// Wire message (kMsgLen = 8 bytes; the net code sends it as its own message type over the existing link):
//   [0] kMagic 0xB7  [1] kVersion 1  [2..3] seq (u16 LE)  [4..5] rev (u16 LE)  [6] flags: bits 0-1 pick (0 none, 1 rematch,
//   2 change characters, 3 leave), bit 2 saw, bits 3-7 zero  [7] check: XOR of bytes 0..6 with 0x5A.
//   decode() refuses a wrong length, magic, version, reserved bit or check byte.
// Sending: take() hands out the current message when the state changed and then again every resend_ms (the peer's liveness and the
// repair of lost messages), and for linger_ms after the outcome (so the peer still gets the final "saw" message after this side moved on).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace jspg {

enum Choice : uint8_t { NONE = 0, REMATCH = 1, CHARSELECT = 2, LEAVE = 3 };
enum Outcome : uint8_t { PENDING = 0, O_REMATCH = 1, O_CHARSELECT = 2, O_LEAVE = 3 };
enum Reason : uint8_t { R_NONE = 0, R_SELF = 1, R_PEER_LEFT = 2, R_LINK_LOST = 3, R_IDLE = 4 };

inline constexpr uint8_t kMagic = 0xB7, kVersion = 1;
inline constexpr size_t kMsgLen = 8;
inline constexpr uint32_t kIdleMs = 180000;       // 3 minutes with no locked pick: Leave (net code: JSP_POSTGAME_IDLE_SECS overrides)
inline constexpr uint32_t kCountdownMs = 30000;   // the view shows "Returning to lobby in N s" only in the last 30 s
inline constexpr uint32_t kSilenceMs = 8000;      // nothing heard from the peer for 8 s: the link is dead
inline constexpr uint32_t kResendMs = 250;
inline constexpr uint32_t kLingerMs = 2000;
inline constexpr const char* kIdleKey = "JSP_POSTGAME_IDLE_SECS";

// JSP_POSTGAME_IDLE_SECS: a whole number of seconds 10..3600; anything else (unset, empty, junk, out of range) = the default.
inline uint32_t idle_ms_from(const char* v)
{
    if (!v || !*v) return kIdleMs;
    uint32_t s = 0;
    for (const char* c = v; *c; c++) { if (*c < '0' || *c > '9' || s > 100000) return kIdleMs; s = s * 10 + (uint32_t)(*c - '0'); }
    return s >= 10 && s <= 3600 ? s * 1000 : kIdleMs;
}

struct Msg { uint16_t seq = 0; uint16_t rev = 0; Choice pick = NONE; bool saw = false; };

inline void encode(const Msg& m, uint8_t out[kMsgLen])
{
    out[0] = kMagic; out[1] = kVersion;
    out[2] = (uint8_t)(m.seq & 0xFF); out[3] = (uint8_t)(m.seq >> 8);
    out[4] = (uint8_t)(m.rev & 0xFF); out[5] = (uint8_t)(m.rev >> 8);
    out[6] = (uint8_t)((m.pick & 3) | (m.saw ? 4 : 0));
    uint8_t x = 0x5A; for (size_t i = 0; i < 7; i++) x ^= out[i];
    out[7] = x;
}
inline bool decode(const uint8_t* p, size_t n, Msg* m)
{
    if (!p || n != kMsgLen || p[0] != kMagic || p[1] != kVersion || (p[6] & 0xF8)) return false;
    uint8_t x = 0x5A; for (size_t i = 0; i < 7; i++) x ^= p[i];
    if (x != p[7]) return false;
    m->seq = (uint16_t)(p[2] | (p[3] << 8)); m->rev = (uint16_t)(p[4] | (p[5] << 8));
    m->pick = (Choice)(p[6] & 3); m->saw = (p[6] & 4) != 0;
    return true;
}

// The outcome of two final picks (neither NONE).
inline Outcome combine(Choice a, Choice b)
{
    if (a == LEAVE || b == LEAVE) return O_LEAVE;
    if (a == CHARSELECT || b == CHARSELECT) return O_CHARSELECT;
    return O_REMATCH;
}

struct Config {
    uint32_t idle_ms = kIdleMs, silence_ms = kSilenceMs, resend_ms = kResendMs, linger_ms = kLingerMs;
};

inline constexpr int kRows = 3;   // row r picks Choice(r + 1) (the native menu's order, NativeRow below)

// What the wait text shows (for the game's own text through a string hook). Printable ASCII.
struct View {
    int sel = 0;                 // the row 0..2 the menu's cursor belongs on (the own pick once it is locked)
    Choice mine = NONE, peer = NONE;
    bool locked = false;         // the own pick is final
    Outcome outcome = PENDING;
    Reason reason = R_NONE;
    std::string status;          // the main line: what happens / what is awaited
    std::string peer_line;       // "Opponent wants a rematch" etc. ("" when unknown or decided)
    std::string countdown;       // "Returning to lobby in N s" in the last kCountdownMs, else ""
    int seconds_left = -1;       // the number in countdown, -1 when not shown
};

class Agreement {
public:
    // A new post-game for match seq. Messages kept aside for this seq are taken now; other kept ones are dropped.
    void start(uint16_t seq, uint64_t now, const Config& c = Config())
    {
        cfg_ = c; seq_ = seq; started_ = true;
        mine_ = NONE; peer_ = NONE; peer_saw_ = false; peer_rev_ = 0; rev_ = 1;
        outcome_ = PENDING; reason_ = R_NONE; t0_ = now; heard_ = now; decided_at_ = 0;
        dirty_ = true; sent_ = false; last_sent_ = 0;
        Msg keep[kKeep]; int nk = 0;
        for (int i = 0; i < nkept_; i++) if (kept_[i].seq == seq) keep[nk++] = kept_[i];
        nkept_ = 0;
        for (int i = 0; i < nk; i++) receive(keep[i], now);
    }
    bool started() const { return started_; }
    uint16_t seq() const { return seq_; }

    // The local player's pick. false: refused (NONE, already locked, or decided). Picking the current pick again is accepted (no change).
    bool pick(Choice c, uint64_t now)
    {
        if (!started_ || c == NONE || outcome_ != PENDING || locked()) return false;
        if (c == mine_) return true;
        mine_ = c; bump();
        if (c == LEAVE) { decide(O_LEAVE, R_SELF, now); return true; }
        try_decide(now);
        return true;
    }
    // A message from the peer. Returns true when it was for this match (it proves the link is alive even when it changes nothing).
    bool receive(const Msg& m, uint64_t now)
    {
        if (!started_ || m.seq != seq_) { keep_aside(m); return false; }
        heard_ = now;
        if (outcome_ != PENDING) return true;
        if (m.rev <= peer_rev_) return true;   // duplicate, or older than what was taken
        peer_rev_ = m.rev;
        const bool first_pick = peer_ == NONE && m.pick != NONE;
        peer_ = m.pick; peer_saw_ = m.saw;
        if (peer_ == LEAVE) { decide(O_LEAVE, R_PEER_LEFT, now); return true; }
        if (first_pick) bump();                // this side's "saw" turns on (and its pick, if any, is locked)
        try_decide(now);
        return true;
    }
    bool receive_bytes(const uint8_t* p, size_t n, uint64_t now) { Msg m; return decode(p, n, &m) && receive(m, now); }
    void link_alive(uint64_t now) { if (started_) heard_ = now; }

    void tick(uint64_t now)
    {
        if (!started_ || outcome_ != PENDING) return;
        if (now - heard_ >= cfg_.silence_ms) {   // unlocked, the pick may still change: it becomes LEAVE, so a peer that hears again leaves too
            if (!locked()) { mine_ = LEAVE; bump(); }
            decide(O_LEAVE, R_LINK_LOST, now); return;
        }
        if (!locked() && now - t0_ >= cfg_.idle_ms) { mine_ = LEAVE; bump(); decide(O_LEAVE, R_IDLE, now); return; }
        if (locked() && now - t0_ >= (uint64_t)cfg_.idle_ms + cfg_.silence_ms) decide(O_LEAVE, R_LINK_LOST, now);
    }
    // The message to send now, if any (after a change at once, else every resend_ms; for linger_ms after the outcome).
    bool take(uint64_t now, Msg* out)
    {
        if (!started_) return false;
        if (outcome_ != PENDING && now - decided_at_ >= cfg_.linger_ms) return false;
        if (!dirty_ && sent_ && now < last_sent_ + cfg_.resend_ms) return false;
        dirty_ = false; sent_ = true; last_sent_ = now;
        *out = current();
        return true;
    }
    Msg current() const { Msg m; m.seq = seq_; m.rev = rev_; m.pick = mine_; m.saw = peer_ != NONE; return m; }

    bool locked() const { return mine_ != NONE && peer_ != NONE; }
    Choice mine() const { return mine_; }
    Choice peer() const { return peer_; }
    Outcome outcome() const { return outcome_; }
    Reason reason() const { return reason_; }

    View view(int sel, uint64_t now) const
    {
        View v;
        v.mine = mine_; v.peer = peer_; v.locked = locked(); v.outcome = outcome_; v.reason = reason_;
        v.sel = (locked() || outcome_ != PENDING) && mine_ != NONE ? (int)mine_ - 1 : (sel < 0 ? 0 : sel >= kRows ? kRows - 1 : sel);
        if (outcome_ != PENDING) { v.status = outcome_text(outcome_, reason_); return v; }
        if (peer_ == REMATCH) v.peer_line = "Opponent wants a rematch";
        else if (peer_ == CHARSELECT) v.peer_line = "Opponent wants to change characters";
        v.status = mine_ != NONE ? "Waiting for opponent..." : peer_ != NONE ? "Choose: your opponent has picked" : "Choose what to do next";
        if (!locked()) {
            const uint64_t el = now - t0_;
            const uint64_t left = el >= cfg_.idle_ms ? 0 : cfg_.idle_ms - el;
            if (left <= kCountdownMs) {
                v.seconds_left = (int)((left + 999) / 1000);
                v.countdown = "Returning to lobby in " + std::to_string(v.seconds_left) + " s";
            }
        }
        return v;
    }
    static const char* outcome_text(Outcome o, Reason r)
    {
        if (o == O_REMATCH) return "Rematch: same characters and stage";
        if (o == O_CHARSELECT) return "Back to character select";
        if (o != O_LEAVE) return "";
        return r == R_PEER_LEFT ? "Opponent left" : r == R_LINK_LOST ? "Connection lost: back to the lobby"
             : r == R_IDLE ? "No choice in time: back to the lobby" : "Back to the lobby";
    }

private:
    static constexpr int kKeep = 4;
    void bump() { rev_++; dirty_ = true; }
    void decide(Outcome o, Reason r, uint64_t now) { outcome_ = o; reason_ = r; decided_at_ = now; dirty_ = true; }
    void try_decide(uint64_t now)
    {
        if (outcome_ == PENDING && mine_ != NONE && peer_ != NONE && peer_saw_) decide(combine(mine_, peer_), R_NONE, now);
    }
    void keep_aside(const Msg& m)
    {
        for (int i = 0; i < nkept_; i++)
            if (kept_[i].seq == m.seq) { if (m.rev > kept_[i].rev) kept_[i] = m; return; }
        if (nkept_ < kKeep) { kept_[nkept_++] = m; return; }
        for (int i = 1; i < kKeep; i++) kept_[i - 1] = kept_[i];   // drop the oldest arrival
        kept_[kKeep - 1] = m;
    }

    Config cfg_;
    bool started_ = false;
    uint16_t seq_ = 0, rev_ = 1, peer_rev_ = 0;
    Choice mine_ = NONE, peer_ = NONE;
    bool peer_saw_ = false;
    Outcome outcome_ = PENDING;
    Reason reason_ = R_NONE;
    uint64_t t0_ = 0, heard_ = 0, decided_at_ = 0, last_sent_ = 0;
    bool dirty_ = false, sent_ = false;
    Msg kept_[kKeep];
    int nkept_ = 0;
};

// ---- The game's own after-fight menu drives the agreement (no drawn page). ----
// Rows of that menu, top to bottom (ASSUMPTION until the menu is mapped in a run: this order, cursor counted from the top):
//   0 "Fight again" = REMATCH, 1 "Re-select character(s)" = CHARSELECT, 2 "Return to main menu" = LEAVE.
enum NativeRow : int { ROW_FIGHT_AGAIN = 0, ROW_RESELECT = 1, ROW_MAIN_MENU = 2 };
inline Choice choice_of_row(int row) { return row == ROW_FIGHT_AGAIN ? REMATCH : row == ROW_RESELECT ? CHARSELECT : row == ROW_MAIN_MENU ? LEAVE : NONE; }
inline int row_of_outcome(Outcome o) { return o == O_REMATCH ? ROW_FIGHT_AGAIN : o == O_CHARSELECT ? ROW_RESELECT : o == O_LEAVE ? ROW_MAIN_MENU : -1; }

// The hook on the native menu's confirm. The game acts on a row only when the agreement says so:
//   - on_confirm(row): the player confirmed a row. Before the outcome it is taken as the pick and HELD (return true: the hook swallows
//     the confirm, the menu stays up with its cursor; the hook shows the wait text from view()). A held pick can be changed by
//     confirming another row until it is locked; after that confirms are held without effect. A confirm of the outcome's row once
//     the outcome is known passes (return false).
//   - run_row(): once the outcome is known, the row the game must now run (it may differ from the local pick: Fight again + Re-select
//     = Re-select); -1 before, and after done(). The hook puts the cursor on it and lets one confirm through (or calls the row's action
//     itself), then calls done(). For LEAVE the game returns to its main menu; the net code then takes the player to the lobby.
//   - on_cancel(): Circle / back on the menu: held while the gate runs (the game's own back would leave without telling the peer).
struct NativeGate {
    Agreement ag;
    bool on = false;
    bool ran = false;
    void start(uint16_t seq, uint64_t now, const Config& c = Config()) { ag.start(seq, now, c); on = true; ran = false; }
    void stop() { on = false; }
    bool on_confirm(int row, uint64_t now)
    {
        if (!on) return false;
        ag.tick(now);
        if (ag.outcome() == PENDING) {
            const Choice c = choice_of_row(row);
            if (c != NONE) ag.pick(c, now);
        }
        if (ag.outcome() != PENDING && row == row_of_outcome(ag.outcome()) && !ran) { ran = true; return false; }
        return true;
    }
    bool on_cancel() const { return on; }
    int run_row(uint64_t now) { if (!on) return -1; ag.tick(now); return ag.outcome() != PENDING && !ran ? row_of_outcome(ag.outcome()) : -1; }
    void done() { ran = true; }
    View view(int cursor_row, uint64_t now) const { return ag.view(cursor_row, now); }
};

}  // namespace jspg
