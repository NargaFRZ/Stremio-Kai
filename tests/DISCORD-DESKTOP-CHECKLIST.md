# Required Discord desktop acceptance

Status: **NOT RUN**. Complete against the generated Windows portable, with a
second account/user viewing the first user's member list or friends activity.
The local payload tests do not satisfy this checklist.

Record Windows version, Kai build-manifest commit, Discord desktop version,
date, and the observer's client/platform. Enable activity sharing in Discord and
RPC in Kai. Allow up to five seconds for the app update queue, then Discord's
own propagation delay. Avoid sharing personal identifiers in screenshots/logs.

| Scenario | Required compact result | Required expanded result | Result |
| --- | --- | --- | --- |
| Movie (e.g. Dune: Part Two) | Watching Dune: Part Two | Poster, movie title, progress/timestamps and existing buttons | Not run |
| TV episode (Breaking Bad S3E7) | Watching Breaking Bad | One Minute (S3-E7), artwork, progress and buttons | Not run |
| Anime episode (e.g. Frieren) | Watching the main anime title | Episode metadata, artwork, progress and buttons | Not run |
| Next episode | Same show title | New episode metadata and reset timer | Not run |
| Another show | New show title | New show's metadata/artwork | Not run |
| Pause/resume | Current show/movie title | Paused state without running timer; corrected timer on resume | Not run |
| Seek | Current title | Playback timer moves to new position | Not run |
| Stop/close player | Existing idle activity or no activity | No previous playback card | Not run |
| Rapid media changes then stop | Final idle/clear state | No queued older title reappears | Not run |
| Delayed metadata | Temporary Stremio then real title | Metadata fills in without restarting playback | Not run |
| Missing poster/button URL | Current title | Remaining valid fields still render | Not run |
| Restart Discord during playback | Current title after reconnect | Current metadata/timer restored | Not run |
| Stop while Discord is closed, reopen | Idle/clear | Old playback does not return | Not run |
| Start Kai with Discord closed, open Discord | Current title | Current playback metadata appears | Not run |
| Disable RPC during playback | No activity after queue drains | Card clears | Not run |
| Restart Kai | Current title after playback starts | No previous-session title retained | Not run |
| Close Kai | No Kai activity | Card disappears after IPC closes | Not run |

Verify compact text from a **server member list, friends list, and collapsed
profile activity** wherever that Discord client displays text. Expanding a card
is not evidence that the compact requirement passes. Capture observer screenshots
of both compact and expanded views for at least one movie, TV episode and anime.

Also exercise normal startup, settings persistence, system tray, Stremio links,
streaming/playback, Kai webmods and existing update behavior. If a regression
appears, record the build manifest, observed behavior and relevant existing logs.

If a Discord client ignores `status_display_type`, record its exact version/view
and the received activity payload. Do not claim success based on the title being
present only in `details`. Do not remove expanded metadata to work around the UI.
