// These checks must also execute in the Windows Release build.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "presence.h"
#include "discord.h"
#include "serialization.h"
#include <cassert>
#include <iostream>
#include <stdexcept>

using namespace kai_discord;

static std::vector<std::string> packets;
static DiscordEventHandlers handlers{};
static bool failSubmit = false;
bool g_isRpcOn = true;

static std::string Serialize(const Presence& presence) {
    char buffer[16384]{};
    const auto rpc = presence.Rpc();
    const auto count = JsonWriteRichPresenceObj(buffer, sizeof(buffer), 1, 1234,
                                               presence.clear ? nullptr : &rpc);
    return std::string(buffer, count);
}

static rapidjson::Document Parse(const std::string& packet) {
    rapidjson::Document doc;
    doc.Parse<rapidjson::kParseValidateEncodingFlag>(packet.c_str());
    assert(!doc.HasParseError());
    assert(std::string(doc["cmd"].GetString()) == "SET_ACTIVITY");
    assert(doc["args"]["pid"].GetInt() == 1234);
    return doc;
}

// Test the production native adapter and real SDK serializer together, replacing
// only the transport entry points. This is not a live Discord client test.
extern "C" void Discord_Initialize(const char* id, DiscordEventHandlers* value, int, const char*) {
    assert(std::string(id) == "1361448446862692492");
    handlers = *value;
}
extern "C" void Discord_UpdatePresence(const DiscordRichPresence* presence) {
    if (failSubmit) throw std::runtime_error("injected RPC submission failure");
    char buffer[16384]{};
    const auto count = JsonWriteRichPresenceObj(buffer, sizeof(buffer), 1, 1234, presence);
    packets.emplace_back(buffer, count);
}
extern "C" void Discord_ClearPresence() { Discord_UpdatePresence(nullptr); }
void AppendToCrashLog(const std::string&) {}

static std::vector<std::string> Movie(const std::string& title = "Dune: Part Two") {
    return {"watching", "movie", title, "", "", "", "", "https://example.com/poster.jpg",
            "60", "7200", "no", "https://www.imdb.com/title/tt15239678/",
            "https://web.stremio.com/#/detail/movie/tt15239678"};
}
static std::vector<std::string> Episode(const std::string& title = "Breaking Bad") {
    auto args = Movie(title);
    args[1] = "series"; args[3] = "3"; args[4] = "7"; args[5] = "One Minute";
    args[6] = "https://example.com/episode.jpg";
    return args;
}

int main() {
    int checks = 0;
    for (const auto& title : {"Dune: Part Two", "Interstellar", "Solo Leveling",
                              "Frieren: Beyond Journey's End", "One Piece"}) {
        const auto p = *Build(Movie(title), 100000);
        auto doc = Parse(Serialize(p));
        const auto& a = doc["args"]["activity"];
        assert(a["type"].GetInt() == 3);
        assert(a["status_display_type"].GetInt() == 2);
        assert(std::string(a["details"].GetString()) == title);
        assert(std::string(a["state"].GetString()) == "Enjoying a Movie");
        assert(std::string(a["assets"]["large_image"].GetString()) == "https://example.com/poster.jpg");
        assert(a["timestamps"]["start"].GetInt64() == 99940);
        assert(a["timestamps"]["end"].GetInt64() == 107140);
        assert(a["buttons"].Size() == 2);
        assert(std::string(a["buttons"][0]["label"].GetString()) == "More Details");
        assert(std::string(a["buttons"][1]["label"].GetString()) == "Watch on Stremio");
        ++checks;
    }

    for (const auto& title : {"Breaking Bad", "Frieren: Beyond Journey's End", "Solo Leveling"}) {
        auto args = Episode(title);
        auto doc = Parse(Serialize(*Build(args, 100000)));
        const auto& a = doc["args"]["activity"];
        assert(std::string(a["details"].GetString()) == title);
        assert(std::string(a["state"].GetString()) == "One Minute (S3-E7)");
        assert(std::string(a["assets"]["small_text"].GetString()) == "One Minute");
        args[10] = "yes";
        doc = Parse(Serialize(*Build(args, 100000)));
        const auto& paused = doc["args"]["activity"];
        assert(paused["status_display_type"].GetInt() == 2);
        assert(std::string(paused["details"].GetString()) == title);
        assert(std::string(paused["state"].GetString()) == "One Minute (S3-E7) - Paused");
        assert(paused["assets"].HasMember("small_image"));
        assert(!paused.HasMember("timestamps"));
        assert(paused["buttons"].Size() == 2);
        checks += 2;
    }

    // Every argument-vector length, including the original 12/13 off-by-one.
    for (size_t count = 1; count <= 13; ++count) {
        auto args = Episode(); args.resize(count);
        auto doc = Parse(Serialize(*Build(args, 100000)));
        assert(doc["args"]["activity"]["type"].GetInt() == 3);
        ++checks;
    }
    auto args = Movie(); args[11].clear();
    auto doc = Parse(Serialize(*Build(args, 100000)));
    assert(doc["args"]["activity"]["buttons"].Size() == 1);
    assert(std::string(doc["args"]["activity"]["buttons"][0]["label"].GetString()) == "Watch on Stremio");
    ++checks;

    for (const auto& value : {"", "NaN", "Infinity", "-1", "1e999", "no time", "123oops"}) {
        args = Movie(); args[8] = value;
        doc = Parse(Serialize(*Build(args, 100000)));
        assert(!doc["args"]["activity"].HasMember("timestamps"));
        ++checks;
    }
    args = Movie(); args[8] = "60.95"; args[9] = "7200.5";
    assert(Build(args, 100000)->start == 99940);
    args[8] = "8000";
    assert(Build(args, 100000)->end == 100000);
    checks += 2;

    const std::string unicode = "Am\xc3\xa9lie \xe2\x80\x94 L\xc3\xa9on";
    assert(Build(Movie(unicode), 100000)->details == unicode);
    args = Movie(std::string(127, 'A') + "\xc3\xa9");
    assert(Build(args, 100000)->details == std::string(127, 'A'));
    args[2] = std::string("Bad\xffTitle\0End", 13);
    doc = Parse(Serialize(*Build(args, 100000)));
    assert(doc["args"]["activity"]["details"].GetStringLength() > 0);
    args = Movie(std::string(10000, 'X')); args[7] = std::string(10000, '"');
    doc = Parse(Serialize(*Build(args, 100000)));
    assert(doc["args"]["activity"]["details"].GetStringLength() == 128);
    assert(!doc["args"]["activity"]["assets"].HasMember("large_image"));
    checks += 4;

    for (const auto& route : {"board", "discover", "library", "calendar", "addons", "settings", "search"}) {
        doc = Parse(Serialize(*Build({route}, 100000)));
        assert(doc["args"]["activity"]["status_display_type"].GetInt() == 0);
        assert(doc["args"]["activity"].HasMember("details"));
        ++checks;
    }
    doc = Parse(Serialize(*Build({"meta-detail", "series", "Breaking Bad", "poster"}, 100000)));
    assert(doc["args"]["activity"]["status_display_type"].GetInt() == 0);
    assert(!Build({}, 100000)); assert(!Build({"unrecognized"}, 100000));
    assert(!Parse(Serialize(*Build({"clear"}, 100000)))["args"].HasMember("activity"));
    checks += 4;

    // Real serialized packets from the publisher: dedupe, late title, rapid
    // changes, seeking, pause/resume, stop, and disconnected cache replacement.
    std::vector<std::string> sent;
    Publisher publisher([&](const Presence& p) { sent.push_back(Serialize(p)); });
    publisher.Connected(true, 0);
    publisher.Set(*Build(Movie(""), 100000), 0);
    assert(sent.size() == 1);
    assert(Parse(sent.back())["args"]["activity"]["status_display_type"].GetInt() == 0);
    publisher.Set(*Build(Episode(), 100001), 1000);
    assert(sent.size() == 1);
    publisher.Pump(5000); assert(sent.size() == 2);
    assert(std::string(Parse(sent.back())["args"]["activity"]["details"].GetString()) == "Breaking Bad");
    args = Episode(); args[8] = "65";
    publisher.Set(*Build(args, 100006), 6000);
    publisher.Pump(10000); assert(sent.size() == 2); // no timer jitter spam
    args[4] = "8"; publisher.Set(*Build(args, 100010), 10000);
    assert(sent.size() == 3);
    publisher.Set(*Build(Movie("Interstellar"), 100011), 11000);
    publisher.Set(*Build(Movie("Dune: Part Two"), 100012), 12000);
    publisher.Pump(15000); assert(sent.size() == 4);
    assert(std::string(Parse(sent.back())["args"]["activity"]["details"].GetString()) == "Dune: Part Two");
    args = Movie(); args[8] = "600";
    publisher.Set(*Build(args, 100020), 20000);
    assert(Parse(sent.back())["args"]["activity"]["timestamps"]["start"].GetInt64() == 99420);
    args[10] = "yes";
    publisher.Set(*Build(args, 100025), 25000);
    assert(!Parse(sent.back())["args"]["activity"].HasMember("timestamps"));
    args[10] = "no";
    publisher.Set(*Build(args, 100030), 30000);
    assert(Parse(sent.back())["args"]["activity"].HasMember("timestamps"));
    publisher.Set({}, 31000); publisher.Pump(35000);
    assert(!Parse(sent.back())["args"].HasMember("activity"));
    publisher.Connected(false, 36000);
    publisher.Set(*Build(Episode("One Piece"), 100036), 36000);
    publisher.Set({}, 36001);
    assert(!Parse(sent.back())["args"].HasMember("activity"));
    auto count = sent.size();
    publisher.Connected(true, 37000); publisher.Pump(45000);
    assert(sent.size() == count); // SDK owns replay; app must not duplicate it
    checks += 11;

    // Actual native adapter error boundary and settings toggle.
    InitializeDiscord();
    handlers.disconnected(1, nullptr);
    SetDiscordPresenceFromArgs(Episode());
    assert(std::string(Parse(packets.back())["args"]["activity"]["state"].GetString()) == "One Minute (S3-E7)");
    SetDiscordPresenceFromArgs({"watching"});
    assert(std::string(Parse(packets.back())["args"]["activity"]["details"].GetString()) == "Stremio");
    g_isRpcOn = false; PumpDiscordPresence();
    assert(!Parse(packets.back())["args"].HasMember("activity"));
    count = packets.size(); PumpDiscordPresence(); assert(packets.size() == count);
    g_isRpcOn = true; failSubmit = true;
    SetDiscordPresenceFromArgs(Movie()); // must contain the injected exception
    failSubmit = false; PumpDiscordPresence();
    assert(std::string(Parse(packets.back())["args"]["activity"]["details"].GetString()) == "Dune: Part Two");
    handlers.errored(1000, nullptr); handlers.ready(nullptr);
    ShutdownDiscordPresenceScheduler();
    checks += 7;

    std::cout << checks << " presence checks passed using the real RPC serializer.\n";
}
