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
| Complete Windows portable ZIP / installer | Packaging in progress; first compilation passed, archive creation stopped on an incorrect license filename (corrected) |
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
checks before writing the final portable ZIP.

After a successful package is produced, the separate desktop checklist is still
required. This task is **incomplete** until the usable Windows package exists
and the requested live verification is performed or explicitly recorded as a
remaining limitation.
