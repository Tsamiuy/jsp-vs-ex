# Rollback thread and result checks

The 64-bit saved-register masks encode thread IDs 1 through 64 in bits 0 through 63. The CRI allowlist, save and restore use the same encoding.

If the periodic registry audit cannot create its Windows worker, the game audits synchronously on that tick and retries startup at the next scheduled audit. After startup the game reads the worker’s reports.

A saved KO or time-up becomes a result when the connected session confirms that tick. On a disconnect the game accepts a saved result only through the last frame verified on a previous live boundary. Otherwise it reports the lost connection. Each new match starts without a confirmation watermark. A corrected input can still reverse a predicted KO. Normal confirmed match ends retain their existing rematch handling.

The existing snapshot ring host test includes the boundary controls. Its binary accepts `--rollback-boundaries` to run only the thread mask, startup failure and result confirmation cases.
