# PR 248 rollback and installer checks

Patch 3005 publishes the completed audio block after queue notification returns. Its idle query reads the queue's active flag, event count and waiter count under the receiver's queue lock. A host fixture pauses the mixer inside notification and pauses a receiver between consuming the event and decrementing its waiter count. It also checks a queued event, an active receiver and a receiver waiting again. These checks cover the target Windows x64 ordering with backend and queue stubs. Native device timing, two-PC behavior and portable synchronization of the other shared audio fields remain unverified.

The installer replaces its named online-play rule for the current installation's executable. The uninstall command uses the same two selectors. A host fixture evaluates the Pascal command arguments with an installation path containing spaces, a custom outbound block, and a second installation. Windows Firewall and UAC remain untested.

The runtime can re-arm the existing link for another match. The postgame helper models result-menu agreement and idle departure. Wiring those choices and the timeout into the live result menu remains pending. Contributor-reported two-copy Windows runs and host handshake/model checks provide different evidence.

Test 2 enables draw skipping during rollback replays by default. The first visible frame after a replay and the performance impact still need target measurements.

The output cushion queries the device before using its capacity because that query can finish a device reopen. A source-derived host control reproduces the missed initial cushion with the earlier order. Cushion arithmetic uses a wide intermediate before the existing device limit is applied. The output fixture also covers fade tails, interpolation, full-buffer drops and the smoothing bypass. These device controls use a simulated queue and do not establish listening quality or native device recovery.
