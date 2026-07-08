#pragma once

#include <string>

void StartSendAudio(const std::string& ip, int port);
void StopSendAudio();
bool IsSendingAudio();