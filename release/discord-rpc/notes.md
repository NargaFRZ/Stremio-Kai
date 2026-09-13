Custom Stremio-Kai 4.8.0 Windows x64 portable with Discord media titles.

Download **Stremio-Kai-4.8.0-RPC-Portable-x64.zip**, extract it completely to a writable folder, and launch **stremio.exe** inside the extracted folder. No development tools are required.

The download is the exact existing build that was tested successfully by the user. Publishing this release does not rebuild or change the application.

Changes:

- Discord's live compact activity uses the current movie or series title, such as `Watching Breaking Bad`, through the supported `status_display_type: 2` field.
- Expanded Rich Presence retains title and episode metadata, artwork, buttons, playback progress and timestamps.
- Playback changes, missing metadata, pauses, stops and reconnects use the existing playback flow, with duplicate updates suppressed.
- The original Kai runtime layout, configuration, UI and embedded executable resources are preserved.

Validation:

- 73 native adapter, RPC serializer and reconnect/queue checks passed, including Windows Release tests.
- The packaged application passed two Windows startup and normal-exit checks.
- The user confirmed the delivered application and requested Discord behavior work correctly. The build runner itself did not have a signed-in Discord desktop client or a second observer account.

Known limitations:

- Discord controls its separate server Recent Activity feed; this release does not guarantee a card or retained viewing history there.
- This is an unsigned custom portable build. An application update that replaces `stremio.exe` can overwrite the custom Discord integration.

Build identity:

- Tag: `v4.8.0-rpc1`, pointing to built commit `ee6af542941f4821e74176cceafb70d312a62942` on `codex/discord-media-title`.
- [Successful Windows build](https://github.com/NargaFRZ/Stremio-Kai/actions/runs/34756954880).
- `SHA256SUMS.txt` verifies both attached ZIP files.
- `Stremio-Kai-RPC-Sources.zip` includes the build recipe and prepared native sources with their licenses.
