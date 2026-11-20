# Privacy policy

Last updated: 2026-11-15

MMF Pocket processes music on your device and contains no advertising, analytics, crash-reporting or tracking SDKs.

## Data handled on the device

The app processes only data selected by the user through Android's system folder picker:

- the selected folder URI and its persisted read permission;
- MMF and WAV file names and contents selected for playback;
- WAV files generated in the app's private cache;
- temporary sound-strength and attack measurements in memory, used only to animate Mafu;
- the selected sound-mode preference.

This data stays on the device. Generated WAV files are not written to public storage. Android may remove cache files when space is needed, and uninstalling the app removes its private app data.

Mafu measures the selected/generated audio locally, not the microphone or other
apps' sound. Its visual measurements are not saved to disk or transmitted.

## Permissions and network access

The app requests the Internet permission solely to check the latest stable release
at `https://api.github.com/repos/Meyonze/mmf-pocket/releases/latest` when you tap
the version label. There are no automatic or background update checks.

The request does not include music, file names, generated audio, usage history or
an app-specific user identifier. GitHub receives normal connection information,
including your IP address and the update-check User-Agent header. If a newer
version exists, the app asks before opening its GitHub release page in your
browser. Browser requests are handled by that browser and GitHub.

The app does not request broad storage permissions. Folder access is read-only
and limited to the folder the user chooses. App backup is disabled.

## Third parties

The app contains no advertising, analytics, crash-reporting or tracking SDKs.

## Contact

For privacy questions, open a GitHub issue that does not contain private files or personal data.
