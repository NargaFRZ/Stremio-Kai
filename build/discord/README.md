# Stremio-Kai: media titles in Discord compact activity

## Status

This is a focused change to the existing native Discord integration, with a
Windows portable packaging workflow. It is not a replacement Stremio app.
The local native-adapter/SDK-serializer tests pass. A Windows build and actual
Discord desktop visual acceptance have **not yet been performed**. A successful
CI artifact contains a build manifest with its Windows test result. It must
still pass the included desktop checklist before compact or expanded rendering
can be called verified.

## Inspection and the relevant field

The current Kai GitHub main commit is
`a5a2a37e818fb5e9406caf65af374fa7fe7700af` (v4.8.0 hotfix). That repository contains
Kai's configuration/webmods, documentation and release archives, but no current
native application sources or installer build scripts. Kai's
[Core Capabilities wiki](https://github.com/allecsc/Stremio-Kai/wiki/%F0%9F%A7%A9-Core-Capabilities)
identifies Community 5.0.21 as its base. The inspected
[Community native implementation](https://github.com/Zaarrg/stremio-community-v5/blob/da0783dfd8e067b97a95d11e33c78936f523c4d3/src/utils/discord.cpp)
uses these fields:

| Field | Existing value | Custom playback value |
| --- | --- | --- |
| Application ID | `1361448446862692492` | Unchanged |
| `type` | `3` (Watching) | Unchanged |
| `details` | Main movie/show title, argument 2 | Same title, bounded valid UTF-8 |
| `status_display_type` | Not set by this implementation; defaults to application name | `2` (Details) when a playback title is available |
| `state` | Movie state or episode title with `(Sseason-Eepisode)` | Same formatting; episode data also retained while paused |
| `assets` | Main poster/title and episode thumbnail/title | Retained |
| `timestamps` | Playback start/end; absent while paused | Retained with safe numeric parsing and jitter suppression |
| `buttons` | More Details and Watch on Stremio | Both retained, including when only the second URL exists |

The native implementation does **not** send an activity `name` string containing
`Stremio`. Discord obtains its default name from the registered application ID.
Changing `largeImageText`, or adding a title to `details` alone, would leave the
default compact display unchanged.

Discord's documented
[Activity object and status display types](https://docs.discord.com/developers/events/gateway-events#activity-object)
allow `status_display_type: 2` to select `details` for the member-list status.
The expected result for `{type: 3, details: "Breaking Bad", status_display_type: 2}`
is **Watching Breaking Bad**. Application identity remains intact. Client versions
and compact view layouts may differ; no local mock can prove how another user's
Discord renders this. The supplied source patch rebuilds both the SDK header and
serializer together, avoiding an incompatible struct/prebuilt-library ABI.

The shipped Kai executable was compared against Community 5.0.21 on the Windows
runner. Every non-resource section, including native code and data, matches byte
for byte. Kai changes the embedded PE resources. Packaging enforces that identity
before building the documented source base, then copies every original Kai
resource onto the rebuilt executable and verifies those resources byte for byte.
A code/data mismatch still stops the build.

## Playback behavior

The existing WebView `activity` event and its metadata vector are reused. There
are no metadata requests or new playback detectors. Argument 2 remains the movie
or show title; episode title/season/number remain separate. An empty title uses
the original application name until a later metadata event supplies the title.
Known idle routes keep their existing idle activity; `clear` clears presence.
Disabling RPC queues a clear. Application exit keeps the existing SDK shutdown.

The latest pending activity replaces superseded changes. Connected updates are
spaced by at least five seconds; identical payloads and normal whole-second timer
jitter do not resend. A title, episode, pause, resume, seek, artwork, button or
clear event is published on the next available slot (normally within five seconds,
plus IPC/client delay). A 250 ms Win32 timer wakes the existing callback loop and
drains this queue; it does not poll playback. The original SDK retains ownership
of IPC retries, reconnect backoff and replay. A small SDK fix prevents a failed
write from overwriting a newer queued title or clear.

Strings used by RPC have owned storage until serialization completes. Missing
arguments, invalid timestamps, malformed UTF-8 and transport exceptions are
handled without terminating playback. Valid Unicode is preserved within the
legacy SDK's 128-byte text limit. Invalid or oversized resource URLs are omitted
rather than truncated. Repeated identical errors are suppressed. Existing debug
output records selected compact text, episode/state, metadata fallback and RPC
submissions; errors also go to the existing crash log. A queued update is not
logged as proof of Discord acceptance.

## Building the portable

The workflow `.github/workflows/discord-windows.yml` runs on a push to
`codex/discord-media-title`, or manually through GitHub Actions. It uses the
existing Community CMake/MSVC/static-runtime build and the official Kai portable
layout. Native/SDK/MPV/header sources and release archive hashes are recorded in
`build/windows/pins.json`.

The current Kai tree has no installer source. The portable route preserves the
official release's bundled runtimes and avoids inventing an installer. Packaging
checks required Node, streaming server, FFmpeg, MPV and fixed WebView2 files and
verifies that unrelated runtime files are unchanged. It does not change settings,
tray behavior, updater configuration or other application logic. The current
GitHub configuration is overlaid on a fresh official portable release.

Expected successful output (these files have **not yet been generated**):

- `dist-rpc/Stremio-Kai-4.8.0-RPC-Portable-x64.zip`
- `dist-rpc/Stremio-Kai-RPC-Sources.zip`
- `dist-rpc/SHA256SUMS.txt`

On a Windows development runner the same workflow can be run with PowerShell 7:

```powershell
./build/windows/build-portable.ps1
```

The runner needs Git, Python, MSVC/Windows SDK, CMake, vcpkg and 7-Zip. These are
**build dependencies only**. The final portable retains the original application's
bundled runtimes; users do not need development tools. Build output records the
actual vcpkg commit, which may vary with the GitHub runner image.

## Launching a generated portable

1. Extract the entire portable ZIP to a writable folder.
2. Close other Stremio/Stremio-Kai instances, then start Discord desktop.
3. Launch `Stremio-Kai-4.8.0-RPC-Portable-x64/stremio.exe`.
4. Keep Discord RPC enabled in the existing Kai settings and play media normally.
5. Follow `CUSTOM-RPC-BUILD/DISCORD-DESKTOP-CHECKLIST.md` with another Discord user.

The build is custom and unsigned. Existing upstream updater behavior is retained;
an upstream update that replaces the native executable may overwrite this change.
No live Windows, playback, startup, installer, Discord restart, or other-account
rendering result should be inferred from the serializer tests.

## Tests

`tests/discord-presence.cpp` compiles the production adapter with the real patched
SDK JSON serializer. Only transport entry points are mocked. Its 67 scenarios
cover movies, TV/anime episodes, original expanded fields, missing/late titles,
Unicode, short vectors, optional buttons, pause/resume/seek, deduplication, rapid
media changes, stop/clear, settings disable, offline cache replacement, and
submission/callback errors. Assertions remain enabled in Windows Release builds.

`tests/discord-rpc-reconnect.cpp` additionally exercises the real SDK queue with
a deterministic transport double: failed writes while a newer title or clear
arrives, replay of the latest offline title/clear, no duplicate replay, and a
fresh queue after application reinitialization. These six checks isolate the
stale-cache race fixed by the SDK patch; they do not launch Discord.

Local Linux validation used GCC C++20 with AddressSanitizer and
UndefinedBehaviorSanitizer. LeakSanitizer was disabled because this execution
environment runs under ptrace; the address/undefined checks remained enabled.
This validates code and RPC payloads, not live Discord desktop acceptance.
