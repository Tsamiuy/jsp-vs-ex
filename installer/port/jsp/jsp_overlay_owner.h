// Serialize the two users of the retained overlay. A visible menu keeps priority
// between redraws; status cannot clear it, including while a session ends.
#ifndef JSP_OVERLAY_OWNER_H
#define JSP_OVERLAY_OWNER_H
#include <atomic>
#include <cstdint>
#include <mutex>
namespace jpo {
inline std::mutex draw_mutex;
inline std::atomic<bool> visible_menu{false};
inline std::atomic<uint64_t> takeover_epoch{0};
inline uint64_t menu_epoch() { return takeover_epoch.load(std::memory_order_acquire); }
inline bool version_surface(bool top_visible, bool has_version, bool notice_visible) { return top_visible && (has_version || notice_visible); }
inline bool menu_active() { return visible_menu.load(std::memory_order_acquire); }
class MenuFrame {
    std::unique_lock<std::mutex> lock_{draw_mutex};
public:
    explicit MenuFrame(bool visible) {
        const bool previous = visible_menu.exchange(visible, std::memory_order_acq_rel);
        if (visible && !previous) takeover_epoch.fetch_add(1, std::memory_order_release);
    }
};
class StatusFrame {
    std::unique_lock<std::mutex> lock_{draw_mutex};
    bool permitted_;
public:
    StatusFrame() : permitted_(!menu_active()) { if (!permitted_) lock_.unlock(); }
    explicit operator bool() const { return permitted_; }
};
} // namespace jpo
#endif
