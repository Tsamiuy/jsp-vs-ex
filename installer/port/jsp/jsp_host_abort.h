// FIX (the frame clock's failure reaches the peer while the guest is stalled): a host-only failure request from the frame clock thread to the
// session's keepalive worker (jsp_net_session.cpp). The worker owns the reaction (NetSession::fail: the local actor leaves, the peer sees the
// disconnect); the frame clock only posts. Retirement (NetSession::stop / fail) never authorizes guest teardown: a retired handle posts nothing,
// so a failure of an ended match cannot end the next session.
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
        return true;   // retirement may still supersede an accepted request
    }
};
using HostAbortHandle = std::shared_ptr<HostAbort>;
}  // namespace jspnet
