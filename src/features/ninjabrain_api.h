#pragma once

#include "features/ninjabrain_data.h"

#include <functional>
#include <stop_token>
#include <string>
#include <thread>

using NinjabrainLogCallback = std::function<void(const std::string&)>;

void ClearNinjabrainStrongholdData(NinjabrainData& data);
void ClearNinjabrainBoatData(NinjabrainData& data);

void ApplyNinjabrainBoatEvent(
    const std::string& payload,
    NinjabrainData& data,
    const NinjabrainLogCallback& logError = {});

void ApplyNinjabrainStrongholdEvent(
    const std::string& payload,
    NinjabrainData& data,
    const NinjabrainLogCallback& logError = {});

std::string NormalizeNinjabrainApiBaseUrl(std::string apiBaseUrl);

struct NinjabrainApiSessionCallbacks {
    std::function<void(const std::string&)> onStrongholdMessage;
    std::function<void()> onStrongholdDisconnect;
    std::function<void(const std::string&)> onBoatMessage;
    std::function<void()> onBoatDisconnect;
    NinjabrainLogCallback onLog;
};

class NinjabrainApiSession {
  public:
    NinjabrainApiSession(std::string apiBaseUrl, NinjabrainApiSessionCallbacks callbacks);
    ~NinjabrainApiSession();

    NinjabrainApiSession(const NinjabrainApiSession&) = delete;
    NinjabrainApiSession& operator=(const NinjabrainApiSession&) = delete;

    void Stop();

  private:
    void RunStream(
        std::stop_token stopToken,
        const char* streamName,
        const char* path,
        const std::function<void(const std::string&)>& onMessage,
        const std::function<void()>& onDisconnect) const;

    std::string apiBaseUrl_;
    NinjabrainApiSessionCallbacks callbacks_;
    std::jthread strongholdThread_;
    std::jthread boatThread_;
};