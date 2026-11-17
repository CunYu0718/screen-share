Screen Share - UNSIGNED VALIDATION BUILD
=========================================

This portable package is produced by GitHub Actions for M1 hardware validation only.
It is not a production release, installer, or security-reviewed distribution.

Before running:
1. Confirm the artifact came from the CunYu0718/screen-share repository and the expected commit SHA.
2. Do not disable Windows security controls globally. If Windows warns about this unsigned build, only continue on a dedicated test machine after verifying its origin.
3. Start the local LiveKit/coturn/Redis/session infrastructure described in infra/README.md.
4. Set LIVEKIT_NODE_IP to the infrastructure host's LAN IPv4 for two-machine testing.
5. Generate short-lived role tokens with `npm run validation:tokens`. Never save tokens in this directory or upload them with validation reports.

Validation:
- Run one host and one viewer on separate Windows machines.
- Synchronize both Windows system clocks before testing. Host validation displays a visible latency marker in the shared monitor's top-left corner.
- Enter and confirm the measured SFU version, bandwidth, RTT, and baseline packet loss in both clients before recording.
- Follow docs/hardware-validation.md from the same repository revision.
- Endpoint reports are written to Documents/ScreenShare/validation.
- The client does not intentionally store screen frames, credentials, URLs, window titles, or participant identifiers in those reports.

This build is unsigned and expires with its GitHub Actions artifact retention period.
