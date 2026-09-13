#include "discord.h"
#include "crashlog.h"
#include "presence.h"
#include <chrono>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

extern bool g_isRpcOn;

namespace {
int64_t MonotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Debug(const std::string& message) {
#ifdef DEBUG_LOG
    std::cout << "[DISCORD]: " << message << std::endl;
#endif
}

std::string lastError;
void Error(const std::string& message) noexcept {
    try {
        if (message == lastError) return;
        lastError = message;
        std::cerr << "[DISCORD]: " << message << std::endl;
        AppendToCrashLog("[DISCORD]: " + message);
    } catch (...) { /* Presence/logging errors must never terminate the player. */ }
}

kai_discord::Publisher publisher([](const kai_discord::Presence& p) {
    if (p.clear) {
        Discord_ClearPresence();
        Debug("Clear queued to RPC");
    } else {
        const auto rpc = p.Rpc();
        Discord_UpdatePresence(&rpc); // SDK serializes synchronously, with owned strings alive.
        Debug("Update queued to RPC; compact=" + (p.statusDisplayType == 2 ? p.details : "Stremio") +
              "; episode/state=" + p.state);
    }
});

void Ready(const DiscordUser*) {
    try {
        lastError.clear();
        publisher.Connected(true, MonotonicMs());
        Debug("Connected; SDK replays the latest queued presence");
    } catch (...) { Error("Could not handle RPC connection"); }
}

void Disconnected(int code, const char* message) {
    try {
        publisher.Connected(false, MonotonicMs());
        Error("Disconnected (" + std::to_string(code) + "): " +
              kai_discord::Text(message ? message : "No error detail", 256));
    } catch (...) { Error("Could not handle RPC disconnection"); }
}

void RpcError(int code, const char* message) {
    try {
        Error("RPC error (" + std::to_string(code) + "): " +
              kai_discord::Text(message ? message : "No error detail", 256));
    } catch (...) { Error("RPC reported an error"); }
}

#ifdef _WIN32
UINT_PTR rpcTimer = 0;
void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD) { PumpDiscordPresence(); }
#endif
} // namespace

void InitializeDiscord() {
    try {
        DiscordEventHandlers handlers{};
        handlers.ready = Ready;
        handlers.disconnected = Disconnected;
        handlers.errored = RpcError;
        Discord_Initialize("1361448446862692492", &handlers, 1, nullptr);
#ifdef _WIN32
        // Wake the existing Win32 message/callback loop while paused or idle.
        // This timer drains the RPC queue; it does not inspect playback.
        rpcTimer = SetTimer(nullptr, 0, 250, Tick);
        if (!rpcTimer) Error("Could not start RPC queue timer");
#endif
    } catch (const std::exception& error) { Error(error.what()); }
      catch (...) { Error("Could not initialize RPC"); }
}

void PumpDiscordPresence() {
    try {
        if (!g_isRpcOn) publisher.Set({}, MonotonicMs());
        publisher.Pump(MonotonicMs());
    } catch (const std::exception& error) { Error(error.what()); }
      catch (...) { Error("Could not publish RPC activity"); }
}

void ShutdownDiscordPresenceScheduler() {
#ifdef _WIN32
    if (rpcTimer) KillTimer(nullptr, rpcTimer);
    rpcTimer = 0;
#endif
    // The existing application Cleanup() still owns Discord_Shutdown().
}

void SetDiscordPresenceFromArgs(const std::vector<std::string>& args) {
    try {
        if (!g_isRpcOn) { publisher.Set({}, MonotonicMs()); return; }
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (auto next = kai_discord::Build(args, now)) {
            if (publisher.Set(std::move(*next), MonotonicMs())) {
                const auto& current = publisher.Current();
                Debug("Media update; title=" + current.details + "; episode/state=" + current.state +
                      (current.fallback ? "; waiting for title metadata" : ""));
            }
        }
    } catch (const std::exception& error) { Error(error.what()); }
      catch (...) { Error("Malformed activity metadata"); }
}
