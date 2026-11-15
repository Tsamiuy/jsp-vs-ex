# Matching2 context ownership

Patch 7906 guards asynchronous Matching2 work against a context slot being
destroyed, stopped, or retired by module termination and then reused. An old
Join response must not replace the new context's room, and an old Leave response
must not clear it. Search and error replies must not publish old callbacks or
event data into a later owner either.

Each slot has a persistent 64-bit owner value outside the resettable context.
Create, Stop, Destroy and Term invalidate prior ownership. Attempting to advance
beyond the maximum value retires the slot instead of wrapping or reactivating
the final token.
Initialization preserves this history. Async operations and deferred signaling
capture an owner value; validation and state/publication changes share the
Matching2 state mutex.

Request admission validates and captures ownership under the state mutex,
releases it for the adapter's connection-status snapshot, and validates the same
owner again before reading defaults, preparing the request, or changing room
data. Missing, stopped and uninitialized contexts reject locally. Start also
revalidates after connecting. No raw context pointer crosses an unlock.

Peer-producing reply and push handlers take the adapter peer mutex before the
Matching2 state mutex. Their locked peer-table update does not perform network
I/O, relock the adapter or invoke a guest callback. Peer registration precedes
the corresponding callback enqueue within the same serialized commit. Other
Matching2 paths never acquire the adapter mutex while holding the state mutex.
The event/signaling mutex remains a leaf after the state mutex. Adapter hooks
are synchronized, copied under the adapter mutex, and invoked after unlocking.
Term leaves the static hooks installed and their module-state guard rejects
later work; it does not reset a transport shared with other NP modules.

This avoids holding the state mutex while waiting behind an adapter request
stalled in network I/O. It does not guarantee a deadline for lifecycle calls:
state transactions still contain allocation, logging, VM access and callback
queue operations. No guest callback is invoked by the state transaction itself.

The focused native tests compile the actual Matching2 and adapter sources with
stubbed wire, VM/coherence, NP startup, signaling and callback-capture seams.
One reentry case uses the actual sysutil completion queue.
They exercise delayed Join/Leave/Search replies, same-context positives and
errors, rejection/free ownership, status and connect gaps, stopped/terminated
contexts, exhaustion, peer/enqueue order and deferred signaling transactions.
A real adapter request holds its mutex inside a controlled fake-send barrier;
Stop, Destroy and Term must finish before that barrier is released. Temporary
source mutants check that these oracles reject missing validation, reversed
lock order, slot history reset and protocol/buffer regressions.

Run these through the project's normal build-resource gate, using a fresh SDK
export. The SDK path must include patch 7906; `--baseline-sdk` should point at
the immediately preceding export so both old stale-room failures are reproduced
alongside the unchanged same-context positive control:

```sh
python installer/tools/matching2_context_test.py --sdk /path/to/new-sdk \
  --baseline-sdk /path/to/previous-sdk --sanitize --mutations \
  --output /path/to/fresh-results
```

Use `--cc clang` for an available pinned Clang compiler. Native sanitizer checks
disable leak detection because fixture VM and SDK event-cache storage live to
process exit; they do check invalid memory accesses and undefined behavior.
Production Windows-target syntax checks are separate from native fixture runs.

The guard does not revoke callbacks already queued or detached by sysutil or
event keys already published. Submission occurs outside the state mutex, so a
request admitted before retirement can still send Create/Join/Leave after
Stop/Destroy/Term returns, including a request not yet submitted when retirement
occurred. There is no remote cancellation or server-cleanup fence. Numeric
handles have no owner identity before first admission. Reordering within one
live owner (including old Leave versus a new room association), untagged room
pushes, malformed/partial room parsing, eager setter rollback and broader
adapter/Score/Signaling global races remain separate work. This patch does not
implement settings exchange, a readiness ACK or a battle-start gate.
