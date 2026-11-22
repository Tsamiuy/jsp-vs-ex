// Host-only failure request. Retirement never authorizes guest teardown.
#pragma once
#include <atomic>
#include <memory>
namespace jspnet {
struct HostAbort {
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
