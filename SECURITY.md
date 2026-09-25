# Security policy

## Supported versions

Security fixes are provided for the latest GitHub release.

## Reporting a vulnerability

Use GitHub's **Private vulnerability reporting** for suspected vulnerabilities. Do not attach a copyrighted, private or malicious MMF to a public issue.

Include, when possible:

- the affected version and Android version;
- a minimal description of the malformed structure;
- steps to reproduce;
- a minimal synthetic reproducer that you are allowed to share.

If a file is essential, wait for a private reporting channel before sharing it. Do not include credentials, personal paths or unrelated media.

## Security model

MMF files are parsed by native C++ code after the user explicitly selects a folder and a file. The app does not register a handler that automatically opens MMF files from other apps, and it has no Internet permission. Input size and decoder resources are bounded, but native file-format parsers should still be treated as an attack surface.
