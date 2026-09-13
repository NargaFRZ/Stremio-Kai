#pragma once

#include "discord_rpc.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kai_discord {

// Discord's RPC text fields are limited to 128 UTF-8 bytes in this SDK.
// Keep whole code points; repair malformed UTF-8 and remove control characters.
inline std::string Text(std::string_view input, size_t limit = 128) {
    std::string out;
    for (size_t i = 0; i < input.size();) {
        const auto c = static_cast<unsigned char>(input[i]);
        size_t count = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 :
                       c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        bool valid = count && i + count <= input.size();
        for (size_t j = 1; valid && j < count; ++j)
            valid = (static_cast<unsigned char>(input[i + j]) & 0xc0) == 0x80;
        if (valid && count >= 3) {
            const auto second = static_cast<unsigned char>(input[i + 1]);
            valid = !(c == 0xe0 && second < 0xa0) && !(c == 0xed && second >= 0xa0) &&
                    !(c == 0xf0 && second < 0x90) && !(c == 0xf4 && second >= 0x90);
        }
        std::string_view part = valid ? input.substr(i, count) : std::string_view("\xef\xbf\xbd");
        if (valid && (c < 0x20 || c == 0x7f)) part = " ";
        if (out.size() + part.size() > limit) break;
        out.append(part);
        i += valid ? count : 1;
    }
    const auto begin = out.find_first_not_of(' ');
    if (begin == std::string::npos) return {};
    return out.substr(begin, out.find_last_not_of(' ') - begin + 1);
}

// Do not truncate URLs into different, broken URLs. Bound malformed input so the
// legacy SDK's fixed serialization buffer cannot be exhausted by metadata.
inline std::string Resource(std::string_view value, size_t limit) {
    if (value.size() > limit) return {};
    for (unsigned char c : value) if (c < 0x20 || c == 0x7f) return {};
    if (Text(value, limit) != value) return {};
    return std::string(value);
}

inline std::optional<int64_t> Seconds(std::string_view value) {
    if (value.empty()) return {};
    double seconds = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), seconds);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        !std::isfinite(seconds) || seconds < 0 || seconds > 315576000) return {};
    return static_cast<int64_t>(seconds);
}

struct Presence {
    bool clear = true;
    bool watching = false;
    bool paused = false;
    bool fallback = false;
    std::string mediaKey, details, state, largeImage, largeText, smallImage, smallText;
    std::string moreDetailsUrl, watchUrl;
    int64_t start = 0, end = 0;
    int statusDisplayType = 0;

    bool operator==(const Presence&) const = default;

    DiscordRichPresence Rpc() const {
        DiscordRichPresence rpc{};
        rpc.type = DISCORD_ACTIVITY_TYPE_WATCHING;
        rpc.statusDisplayType = statusDisplayType;
        rpc.details = details.c_str();
        rpc.state = state.c_str();
        rpc.largeImageKey = largeImage.c_str();
        rpc.largeImageText = largeText.c_str();
        rpc.smallImageKey = smallImage.c_str();
        rpc.smallImageText = smallText.c_str();
        rpc.startTimestamp = start;
        rpc.endTimestamp = end;
        if (!moreDetailsUrl.empty()) {
            rpc.button1Label = "More Details";
            rpc.button1Url = moreDetailsUrl.c_str();
        }
        if (!watchUrl.empty()) {
            rpc.button2Label = "Watch on Stremio";
            rpc.button2Url = watchUrl.c_str();
        }
        return rpc;
    }
};

// Consumes the existing WebView -> native "activity" argument vector.
// No new playback watcher, metadata provider, or title lookup is introduced.
inline std::optional<Presence> Build(const std::vector<std::string>& args, int64_t now) {
    if (args.empty()) return {};
    const auto arg = [&](size_t index) -> std::string_view {
        return index < args.size() ? std::string_view(args[index]) : std::string_view{};
    };
    Presence p;
    if (arg(0) == "clear") return p;
    p.clear = false;

    if (arg(0) == "watching" || arg(0) == "meta-detail") {
        p.watching = arg(0) == "watching";
        p.details = Text(arg(2));
        p.fallback = p.details.empty();
        if (p.fallback) p.details = "Stremio";
        // This controls the compact member-list text. Keeping the application
        // name untouched preserves its identity and the expanded activity card.
        p.statusDisplayType = p.watching && !p.fallback ? DISCORD_STATUS_DISPLAY_TYPE_DETAILS
                                                       : DISCORD_STATUS_DISPLAY_TYPE_NAME;
        p.largeText = p.details;
        p.largeImage = Resource(arg(p.watching ? 7 : 3), 2048);
        if (!p.watching) {
            p.state = arg(1) == "movie" ? "Exploring a Movie" : "Exploring a Series";
            return p;
        }

        const std::string season = Text(arg(3), 16), episode = Text(arg(4), 16);
        const std::string episodeTitle = Text(arg(5));
        const bool series = arg(1) == "series" || (!season.empty() && !episode.empty());
        p.mediaKey = std::string(arg(1)) + "\n" + p.details + "\n" + season + "\n" + episode;
        p.paused = arg(10) == "yes";
        if (series) {
            std::string episodeState = episodeTitle;
            if (!season.empty() || !episode.empty()) {
                if (!episodeState.empty()) episodeState += " ";
                episodeState += "(S" + season + "-E" + episode + ")";
            }
            // Keep the useful episode metadata and artwork when paused, too.
            p.state = Text(episodeState, p.paused ? 117 : 128);
            if (p.paused) p.state += p.state.empty() ? "Paused" : " - Paused";
            p.smallImage = Resource(arg(6), 2048);
            if (!p.smallImage.empty()) p.smallText = episodeTitle;
        } else {
            p.state = p.paused ? "Paused" : "Enjoying a Movie";
        }

        if (!p.paused) {
            const auto elapsed = Seconds(arg(8)), duration = Seconds(arg(9));
            if (elapsed && duration && *duration > 0 && now >= 0 && now < 253402300800LL) {
                const auto position = std::min(*elapsed, *duration);
                p.start = now - position;
                p.end = p.start + *duration;
            }
        }
        p.moreDetailsUrl = Resource(arg(11), 512);
        p.watchUrl = Resource(arg(12), 512);
        return p;
    }

    struct Idle { const char* route; const char* details; const char* state; };
    constexpr Idle idle[] = {
        {"board", "Resuming Favorites", "On Board"},
        {"discover", "Finding New Gems", "In Discover"},
        {"library", "Revisiting Old Favorites", "In Library"},
        {"calendar", "Planning My Next Binge", "On Calendar"},
        {"addons", "Exploring Add-ons", "In Add-ons"},
        {"settings", "Tuning Preferences", "In Settings"},
        {"search", "Searching for Shows & Movies", "In Search"}
    };
    for (const auto& entry : idle) if (arg(0) == entry.route) {
        p.details = entry.details;
        p.state = entry.state;
        p.largeImage = "https://raw.githubusercontent.com/Stremio/stremio-web/refs/heads/development/images/icon.png";
        p.largeText = "Stremio";
        return p;
    }
    return {};
}

// A latest-value queue for RPC publication, separate from playback detection.
// The original RPC library owns IPC reconnect/backoff and replays its last
// queued packet on connection. Keep that packet current while disconnected.
class Publisher {
public:
    using Sink = std::function<void(const Presence&)>;
    explicit Publisher(Sink sink) : send(std::move(sink)) {}

    bool Set(Presence next, int64_t monotonicMs) {
        if (next.watching && !next.paused && desired.watching && !desired.paused &&
            next.mediaKey == desired.mediaKey && next.start && desired.start &&
            next.end - next.start == desired.end - desired.start &&
            std::abs(next.start - desired.start) <= 2) {
            // Whole-second playback samples can wobble by a second. Keep the
            // existing Discord timer unless a seek/resume/duration change occurs.
            next.start = desired.start;
            next.end = desired.end;
        }
        const bool changed = next != desired;
        desired = std::move(next);
        Pump(monotonicMs);
        return changed;
    }

    void Connected(bool value, int64_t monotonicMs) {
        connected = value;
        Pump(monotonicMs);
    }

    void Pump(int64_t monotonicMs) {
        if (desired == submitted) return;
        if (connected && lastSubmit && monotonicMs - *lastSubmit < 5000) return;
        send(desired);
        submitted = desired;
        lastSubmit = monotonicMs;
    }

    const Presence& Current() const { return desired; }

private:
    Sink send;
    Presence desired, submitted;
    std::optional<int64_t> lastSubmit;
    bool connected = false;
};
} // namespace kai_discord
