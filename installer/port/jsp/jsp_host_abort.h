// Host-only failure request. Retirement never authorizes guest teardown.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
namespace jspnet {
struct HostAbort {
    explicit HostAbort(uint64_t notice = 0) : notice_id(notice) {}
    const uint64_t notice_id;
    std::atomic<bool> retired{false};
    std::atomic<bool> requested{false};
    bool post() {
        if (retired.load()) return false;
        requested.store(true);
        return true; // retirement may supersede an accepted request
    }
};
using HostAbortHandle = std::shared_ptr<HostAbort>;
} // namespace jspnet
