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
| PowerShell execution / Windows CMake compilation | Not run: no Windows build runner available yet |
| Official Kai native EXE versus documented Community base | Not run: enforced before substitution by the Windows workflow |
| Complete Windows portable ZIP / installer | Not generated |
| Live Discord compact activity from another account | Not run |
| Live expanded activity and full playback regressions | Not run |

The payload tests confirm `type: 3`, `status_display_type: 2`, and the main
movie/show title in `details`, while retaining expanded episode metadata,
artwork, both buttons and valid timestamps. They do not prove that any particular
Discord client renders these fields as intended.

The user has now created `https://github.com/NargaFRZ/Stremio-Kai`. Its main
branch matches the inspected upstream commit, and the user's repository role
includes push permission. However, GitHub rejected the prepared tree upload
with HTTP 403, `Resource not accessible by integration`. The app's installation
and installed-account lists do not include `NargaFRZ`. No remote changes or
Windows workflow runs were created. The connected GitHub app must be installed
or configured for that account and granted access to this fork before the
prepared branch/workflow can be pushed.

After a successful package is produced, the separate desktop checklist is still
required. This task is **incomplete** until the usable Windows package exists
and the requested live verification is performed or explicitly recorded as a
remaining limitation.
