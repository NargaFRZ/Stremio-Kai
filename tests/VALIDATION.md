# Validation record — 2026-09-13

Base Kai main: `a5a2a37e818fb5e9406caf65af374fa7fe7700af`, rechecked against GitHub.
The changes are limited to build/Discord files and tests; the current Kai
configuration, webmods and existing application logic are unchanged.

| Check | Result |
| --- | --- |
| Native adapter plus actual patched SDK serializer | 67 checks passed |
| Actual SDK queue/replay with injected transport failures | 6 checks passed |
| AddressSanitizer and UndefinedBehaviorSanitizer | Passed for both test programs |
| LeakSanitizer | Not available under this environment's ptrace execution; disabled explicitly |
| Source preparation against clean pinned native/SDK checkouts | Passed; prepared adapter bytes match tested source |
| Native and SDK patch whitespace validation | Passed |
| Python preparation script and workflow YAML parsing | Passed |
| PowerShell execution / Windows CMake compilation | Passed on Windows Server 2025 with Visual Studio 2026 (MSVC 14.51 toolset) |
| Windows Release adapter/serializer and SDK queue tests | Both test programs passed (73 checks) |
| Original Kai resources on rebuilt native executable | All 12 resources preserved and verified byte for byte |
| Official Kai native EXE versus documented Community base | All non-resource sections match Community 5.0.21 byte for byte; Kai has different embedded resources |
| Packaged Windows app startup and normal exit, repeated after restart | Passed twice; native window, MPV initialization, bundled Node/WebView2, and WebView-to-native activity events |
| Complete Windows portable ZIP | Generated and uploaded successfully, 561,570,338 bytes |
| Live Discord compact activity from another account | Not run |
| Live expanded activity and full playback regressions | Not run |

The payload tests confirm `type: 3`, `status_display_type: 2`, and the main
movie/show title in `details`, while retaining expanded episode metadata,
artwork, both buttons and valid timestamps. They do not prove that any particular
Discord client renders these fields as intended.

The authorized changes are on `NargaFRZ/Stremio-Kai`, branch
`codex/discord-media-title`. GitHub access is working and Windows workflow runs
have started. Run `34730381554` established the native code/data identity; the
initial whole-file comparison correctly detected Kai's different PE resources.
The packaging workflow now verifies native code/data identity and preserves all
original Kai resources, with a byte-for-byte verification after copying them.
Run `34756303061` compiled the Windows executable, passed both CTest programs,
preserved all resources and passed runtime-file completeness/integrity checks.
Final archive creation stopped at `LICENSE` instead of the existing `LICENSE.md`.
The recipe now uses the correct filename and adds two packaged-app launch/exit
checks before writing the final portable ZIP. Both passed in the successful run
below. The smoke check uses a local fixture, without playing actual media or
connecting to Discord; it does not demonstrate another user's Discord rendering.

## Download and build identity

- Successful Windows run: [34756954880](https://github.com/NargaFRZ/Stremio-Kai/actions/runs/34756954880)
- Built source commit: `ee6af542941f4821e74176cceafb70d312a62942`
- [Download the Windows artifact](https://github.com/NargaFRZ/Stremio-Kai/actions/runs/34756954880/artifacts/10316913425)
- Artifact: `Stremio-Kai-4.8.0-RPC-Windows-x64.zip`, 565,370,640 bytes
- Artifact SHA-256: `699c2367f2f24ba7336422ec3c85a0e92278a459b81beb774b73394305f39f52`
- GitHub artifact expiry: 2026-10-13 12:35:55 UTC

The artifact contains `Stremio-Kai-4.8.0-RPC-Portable-x64.zip`,
`Stremio-Kai-RPC-Sources.zip` and `SHA256SUMS.txt`. The per-file checksums are in
that text file; the hash above applies to the complete outer GitHub artifact.
The runner output location was
`D:\a\Stremio-Kai\Stremio-Kai\dist-rpc\Stremio-Kai-4.8.0-RPC-Portable-x64.zip`.

Extract the outer artifact, then extract the portable ZIP completely to a
writable folder and launch `Stremio-Kai-4.8.0-RPC-Portable-x64/stremio.exe`.
No development tools are needed. The current repository has no installer
recipe, so this delivers the requested portable fallback using Kai's original
runtime layout.

## Remaining acceptance limits

The Windows package is ready to run. **Actual Discord compact and expanded
rendering remains unverified** because this environment has no signed-in Discord
desktop client and observer account. Real movie/TV/anime playback, next-episode
behavior, live Discord restart, and full settings/tray/updater regressions still
need the separate desktop checklist. Payload and runtime tests are not a
substitute for that acceptance check.

The build is custom and unsigned. Existing updater behavior is retained; an
upstream update that replaces `stremio.exe` can overwrite the custom integration.
