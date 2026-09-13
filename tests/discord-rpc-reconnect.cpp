// Exercise the real SDK queue/replay code with a deterministic failing transport.
// This does not connect to Discord or emulate its UI.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "discord_rpc.h"
#include "rpc_connection.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

static RpcConnection* peer = nullptr;
static std::vector<std::string> delivered;
static std::function<void()> duringFailedWrite;

int GetProcessId() { return 1234; }
extern "C" void Discord_Register(const char*, const char*) {}
extern "C" void Discord_RegisterSteamGame(const char*, const char*) {}

RpcConnection* RpcConnection::Create(const char*) { return peer = new RpcConnection{}; }
void RpcConnection::Destroy(RpcConnection*& value) { delete value; value = nullptr; peer = nullptr; }
void RpcConnection::Open() {
    state = State::Connected;
    JsonDocument ready;
    char packet[] = R"({"data":{"user":{"id":"1234","username":"test"}}})";
    ready.ParseInsitu(packet); // the SDK's real transport uses in-place parsing
    if (onConnect) onConnect(ready);
}
void RpcConnection::Close() {
    state = State::Disconnected;
    if (onDisconnect) onDisconnect(1, "Injected disconnect");
}
bool RpcConnection::Read(JsonDocument&) { return false; }
bool RpcConnection::Write(const void* bytes, size_t length) {
    if (duringFailedWrite) {
        auto action = std::exchange(duringFailedWrite, {});
        action(); // a newer event arrives after the SDK copied its local packet
        return false;
    }
    delivered.emplace_back(static_cast<const char*>(bytes), length);
    return true;
}

static void SetTitle(const char* title) {
    DiscordRichPresence p{};
    p.type = DISCORD_ACTIVITY_TYPE_WATCHING;
    p.statusDisplayType = DISCORD_STATUS_DISPLAY_TYPE_DETAILS;
    p.details = title;
    Discord_UpdatePresence(&p);
}
static std::string LastTitle() {
    rapidjson::Document value;
    value.Parse(delivered.back().c_str());
    assert(!value.HasParseError());
    if (!value["args"].HasMember("activity")) return {};
    assert(value["args"]["activity"]["status_display_type"].GetInt() == 2);
    return value["args"]["activity"]["details"].GetString();
}

int main() {
    Discord_Initialize("1361448446862692492", nullptr, 0, nullptr);
    SetTitle("Breaking Bad");
    peer->Open();
    Discord_UpdateConnection();
    assert(LastTitle() == "Breaking Bad");

    // Both regressions fail with the original QueuedPresence.Copy(local).
    SetTitle("Old episode");
    duringFailedWrite = [] { SetTitle("Frieren: Beyond Journey's End"); };
    Discord_UpdateConnection();
    Discord_UpdateConnection();
    assert(LastTitle() == "Frieren: Beyond Journey's End");

    SetTitle("Old movie");
    duringFailedWrite = [] { Discord_ClearPresence(); };
    Discord_UpdateConnection();
    Discord_UpdateConnection();
    assert(LastTitle().empty());

    // Reconnect sends the most recent offline state once, including a clear.
    peer->Close();
    SetTitle("One Piece");
    SetTitle("Dune: Part Two");
    peer->Open();
    Discord_UpdateConnection();
    assert(LastTitle() == "Dune: Part Two");
    auto count = delivered.size();
    Discord_UpdateConnection();
    assert(delivered.size() == count);
    peer->Close();
    Discord_ClearPresence();
    peer->Open();
    Discord_UpdateConnection();
    assert(LastTitle().empty());

    // A new application initialization starts with an empty SDK queue.
    Discord_Shutdown();
    count = delivered.size();
    Discord_Initialize("1361448446862692492", nullptr, 0, nullptr);
    peer->Open();
    Discord_UpdateConnection();
    assert(delivered.size() == count);
    Discord_Shutdown();
    std::cout << "6 real-SDK queue/reconnect checks passed with simulated transport.\n";
}
