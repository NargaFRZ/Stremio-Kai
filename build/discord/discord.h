#pragma once
#include <string>
#include <vector>

void InitializeDiscord();
void SetDiscordPresenceFromArgs(const std::vector<std::string>& args);
void PumpDiscordPresence();
void ShutdownDiscordPresenceScheduler();
