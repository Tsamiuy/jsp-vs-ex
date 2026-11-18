// jsp_step_input.h -- canonical per-tick, per-player-slot input for the rollback stepper. Pure: no game, no runtime.
// Host test: installer/tools/stepper_test.cpp (installer/tools/stepper_test.sh).
//
// Tick numbering: tick t is the scene pass that follows t-1 completed passes (the fight hash's `k`: the value of g_done + 1 when the pad is
// read for it; the pad manager is polled once per loop iteration in BeginFrame, before the scene pass). A
// session input for tick t is what the simulation sees in the pad read that feeds scene pass t.
//
// Canonical form (what the rollback session puts on the wire, 8 bytes): buttons u16 (DIGITAL2 << 8 | DIGITAL1, the cellPad packing), four sticks u8
// (centre 128), connected. Everything else cellPadData carries (pressure, sensors, len) is DERIVED from these in to_record(), never read from
// the host pad, so two peers cannot differ by it. ASSUMPTION: the game does not use analog pressure beyond the digital state (a PRESS_ON
// probe in the first GPU run says); if it does, `lt`/`rt` bytes join the canonical form and kPackBits grows.
//
// Edges (press/release/hold counts/key repeat) are NOT part of the input: the game derives them in the pad device object (the game's timing notes
// 5.5 item 5a: +0x24 = +0x20, press = cur & ~prev ...) from guest memory that the snapshot restores, so a replay of tick t sees the same
// previous state as the first run did.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace jsi {

constexpr int kSlots = 4;              // players; slot s is cellPad port s (ASSUMPTION: the game numbers its pads like the ports)
constexpr uint32_t kRing = 256;        // ticks of history (a power of two); depth of the ring + prediction window must stay below it
constexpr uint32_t kNone = 0xFFFFFFFFu;

struct Pad {
    uint16_t buttons = 0;
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
    bool connected = false;
    bool operator==(const Pad& o) const { return buttons == o.buttons && lx == o.lx && ly == o.ly && rx == o.rx && ry == o.ry && connected == o.connected; }
    bool operator!=(const Pad& o) const { return !(*this == o); }
    // 8-byte wire form: little-endian fields, fixed layout (byte order on the wire must not depend on the host).
    std::array<uint8_t, 8> pack() const {
        return {uint8_t(buttons & 0xFF), uint8_t(buttons >> 8), lx, ly, rx, ry, uint8_t(connected ? 1 : 0), 0};
    }
    static bool unpack(const uint8_t* b, size_t n, Pad* out) {   // false for a short or non-canonical blob (reserved byte, flag > 1)
        if (n != 8 || b[6] > 1 || b[7] != 0) return false;
        out->buttons = uint16_t(b[0] | (b[1] << 8)); out->lx = b[2]; out->ly = b[3]; out->rx = b[4]; out->ry = b[5]; out->connected = b[6] == 1;
        return true;
    }
    uint64_t key() const { auto p = pack(); uint64_t k; std::memcpy(&k, p.data(), 8); return k; }
};

// cellPadData record offsets (libs/input/cellPad.h) and lengths.
constexpr int kOffDigital1 = 2, kOffDigital2 = 3, kOffRX = 4, kOffRY = 5, kOffLX = 6, kOffLY = 7, kOffPressRight = 8, kOffPressL2 = 18, kOffPressR2 = 19,
              kOffSensorX = 20, kOffSensorG = 23, kMaxCodes = 64;
constexpr int kLenDefault = 8, kLenPress = 24, kLenSensor = 28;

// Fill a cellPadData record (host u16 array, not yet byte-swapped) from a canonical pad. `setting_len` = the length the port's setting selects
// (8 / 24 / 28); the packet length is always that (never 0 here: the caller decides whether this call gets a packet, see Table::serve).
// Pressure bytes of a pressed digital button are 0xFF, sensors are at rest (512 / 399 / 512 / 512, as the runtime's default).
inline void to_record(const Pad& p, int setting_len, uint16_t* button, int32_t* len)
{
    for (int i = 0; i < kMaxCodes; i++) button[i] = 0;
    *len = setting_len;
    button[0] = 0;
    button[1] = uint16_t(0x70 | ((setting_len / 2) & 0xF));
    button[kOffDigital1] = uint16_t(p.buttons & 0xFF);
    button[kOffDigital2] = uint16_t(p.buttons >> 8);
    button[kOffRX] = p.rx; button[kOffRY] = p.ry; button[kOffLX] = p.lx; button[kOffLY] = p.ly;
    if (setting_len >= kLenPress) {
        // PRESS_RIGHT, LEFT, UP, DOWN (bits 5, 7, 4, 6 of DIGITAL1), TRIANGLE, CIRCLE, CROSS, SQUARE (12..15), L1, R1, L2, R2 (10, 11, 8, 9)
        static const int bit[12] = {5, 7, 4, 6, 12, 13, 14, 15, 10, 11, 8, 9};
        for (int i = 0; i < 12; i++) button[kOffPressRight + i] = (p.buttons >> bit[i]) & 1 ? 0xFF : 0;
    }
    if (setting_len >= kLenSensor) { button[kOffSensorX] = 512; button[kOffSensorX + 1] = 399; button[kOffSensorX + 2] = 512; button[kOffSensorG] = 512; }
}

// Inverse for the capture side: read the canonical fields back from a record the host pad layer filled.
inline Pad from_record(const uint16_t* button, bool connected)
{
    Pad p;
    p.buttons = uint16_t((button[kOffDigital1] & 0xFF) | ((button[kOffDigital2] & 0xFF) << 8));
    p.rx = uint8_t(button[kOffRX]); p.ry = uint8_t(button[kOffRY]); p.lx = uint8_t(button[kOffLX]); p.ly = uint8_t(button[kOffLY]);
    p.connected = connected;
    return p;
}

// The input history of a session: for every tick in the ring and every slot, the value, whether it is confirmed (a remote input that arrived,
// or a local one) or predicted, and what the simulation actually USED (so a late correction of an already simulated tick is detected here as well,
// independently of the netcode library).
class Table {
public:
    struct Cell { uint32_t tick = kNone; Pad pad; bool confirmed = false; bool used = false; Pad used_pad; };

    // Slots that exist in this session (bit s = slot s is a player). A slot outside the mask is served as "not connected" (len 0, neutral).
    void set_mask(uint32_t m) { mask_ = m & ((1u << kSlots) - 1); }
    uint32_t mask() const { return mask_; }

    // The netcode (or a script) says what slot `s` presses at tick `t`. Returns true when this changes a value the simulation already used
    // (that is a misprediction: first_wrong() then names the earliest such tick).
    bool set(uint32_t t, int s, const Pad& p, bool confirmed) {
        Cell& c = cell(t, s);
        if (c.tick != t) { c = Cell(); c.tick = t; }
        const bool wrong = c.used && c.used_pad != p;
        c.pad = p; c.confirmed = confirmed || c.confirmed;
        if (wrong && (wrong_from_ == kNone || t < wrong_from_)) wrong_from_ = t;
        return wrong;
    }
    bool has(uint32_t t, int s) const { const Cell& c = cell(t, s); return c.tick == t; }
    bool confirmed(uint32_t t, int s) const { const Cell& c = cell(t, s); return c.tick == t && c.confirmed; }
    Pad get(uint32_t t, int s) const { const Cell& c = cell(t, s); return c.tick == t ? c.pad : Pad(); }

    // Local hardware, once per NEW tick PER SLOT: stores `hw` only for a slot in the mask, when no value exists for (t, s) and t is beyond everything
    // captured for that slot before, so a replay of an old tick never samples the device again. Returns true when it stored.
    bool capture_local(uint32_t t, int s, const Pad& hw) {
        if (s < 0 || s >= kSlots || !(mask_ & (1u << s))) return false;   // a slot outside the session is never captured (and costs nothing)
        if (has(t, s) || (cap_top_[s] != kNone && t <= cap_top_[s])) return false;
        set(t, s, hw, true);
        cap_top_[s] = t;
        return true;
    }

    // The pad read of the simulation for tick t, slot s: the first call of a tick (per slot) gets a packet (true + the value, marked used);
    // later calls in the same tick get none (len 0: the drain contract of cellPadGetData). A slot outside the mask never gets a packet.
    // A tick with no value yet is served neutral and `starved` counts it (the caller decides whether that is an error: it is for netplay).
    bool serve(uint32_t t, int s, Pad* out) {
        if (s < 0 || s >= kSlots || !(mask_ & (1u << s))) return false;
        if (served_[s] != kNone && served_[s] == t) return false;
        served_[s] = t;
        Cell& c = cell(t, s);
        if (c.tick != t) { starved_++; *out = Pad(); out->connected = true; c = Cell(); c.tick = t; c.pad = *out; }
        else *out = c.pad;
        out->connected = true;
        c.used = true; c.used_pad = c.pad;
        return true;
    }
    uint32_t starved() const { return starved_; }

    // After a rollback to `done` completed ticks the next tick is done+1: serving restarts there, everything used beyond is forgotten
    // (it will be used again by the replay) and the misprediction mark is cleared (the rollback is the correction).
    void rewind(uint32_t done) {
        for (int s = 0; s < kSlots; s++) { if (served_[s] != kNone && served_[s] > done) served_[s] = done; }
        for (auto& row : ring_) for (Cell& c : row) if (c.tick != kNone && c.tick > done) { c.used = false; }
        wrong_from_ = kNone;
    }
    uint32_t first_wrong() const { return wrong_from_; }

    // Drop history older than `t` (the ring evicted that tick; nothing can roll back to it any more).
    void forget_before(uint32_t t) { for (auto& row : ring_) for (Cell& c : row) if (c.tick != kNone && c.tick < t) c = Cell(); }

private:
    Cell& cell(uint32_t t, int s) { return ring_[t % kRing][s]; }
    const Cell& cell(uint32_t t, int s) const { return ring_[t % kRing][s]; }
    std::array<std::array<Cell, kSlots>, kRing> ring_{};
    uint32_t served_[kSlots] = {kNone, kNone, kNone, kNone};
    uint32_t mask_ = 0, wrong_from_ = kNone, starved_ = 0;
    uint32_t cap_top_[kSlots] = {kNone, kNone, kNone, kNone};   // per slot: the order in which a game reads its ports must not matter
};

}  // namespace jsi
