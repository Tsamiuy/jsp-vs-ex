# Security

## Reporting a problem

Please report security problems privately to the maintainers, not in a public issue.
Use GitHub's private vulnerability reporting for this repository (Security tab, "Report a vulnerability")
if it is available. If it is not, open an issue that says only that you have a private security report,
and ask for a contact. Do not put details in that issue.

## What to leave out

- No secrets in issues, pull requests or logs: no passwords, tokens, keys or personal paths.
- No game files, no keys and no decrypted game data. See `README.md`.

## Scope

The repository holds source code, notes and Windows scripts. Some scripts start programs, stop processes or
change system settings (for example `tools/boost/`, which changes the active Windows power plan).
Reports about what these scripts do to a machine are welcome.
