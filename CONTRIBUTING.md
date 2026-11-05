# Contributing

Thanks for looking. This is a small work-in-progress fan project. Keep changes small and easy to review.

## Rules

- No game files. That means no ISO, EBOOT, decrypted ELF, firmware, recompiled game code, captures or
  screenshots of the game.
- No keys, tokens or personal data. Use variables or relative paths instead of your own user folder.
- Fixes to ps3recomp itself belong in the ps3recomp repository. See `NOTICE.md`.
- If you used AI help, say so in general terms in the pull request. Naming a tool is not needed.

## Sending a change

1. Open an issue first for anything bigger than a small fix or a wording change.
2. Describe what the change does and why.
3. Add a short note on how you tested it: what you built or ran, and on which Windows version.
   If you did not test it, say so.
4. One topic per pull request. Changes reach `main` only through a pull request that the maintainer has
   read and merged, after CI is green. AI coding agents follow [AGENTS.md](AGENTS.md).

## Notes and patches

The notes in `docs/notes/` are a working log. When you add to them, state what was measured and what is
only a guess, and keep game data out of them.
