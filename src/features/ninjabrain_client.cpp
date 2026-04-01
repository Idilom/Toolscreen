#include "ninjabrain_client.h"

#include "features/ninjabrain_api.h"

#include "common/utils.h"
#include "config/config_defaults.h"
#include "gui/gui.h"

#include <memory>
#include <mutex>

namespace {

std::mutex g_ninjabrainClientMutex;
NinjabrainApiConnectionTracker g_ninjabrainClientStatus;

void LogNinjabrainMessage(const std::string& message) {
    LogCategory("ninjabrain", message);
}

std::string ResolveApiBaseUrl() {
    std::string apiBaseUrl = ConfigDefaults::CONFIG_NINJABRAIN_API_BASE_URL;

    if (const auto configSnapshot = GetConfigSnapshot()) {
        apiBaseUrl = configSnapshot->ninjabrainOverlay.apiBaseUrl;
    } else {
        apiBaseUrl = g_config.ninjabrainOverlay.apiBaseUrl;
    }

    return NormalizeNinjabrainApiBaseUrl(std::move(apiBaseUrl));
}

std::unique_ptr<NinjabrainApiSession> g_ninjabrainClientSession;

} // namespace

void StartNinjabrainClient() {
    std::lock_guard<std::mutex> lock(g_ninjabrainClientMutex);
    if (g_ninjabrainClientSession) { return; }

    const std::string apiBaseUrl = ResolveApiBaseUrl();

    {
        std::lock_guard<std::mutex> dataLock(g_ninjabrainDataMutex);
        g_ninjabrainData = NinjabrainData{};
    }

    g_ninjabrainClientStatus.Start(apiBaseUrl);

    NinjabrainApiSessionCallbacks callbacks;
    callbacks.onStrongholdMessage = [](const std::string& payload) {
        std::lock_guard<std::mutex> lock(g_ninjabrainDataMutex);
        ApplyNinjabrainStrongholdEvent(payload, g_ninjabrainData, LogNinjabrainMessage);
    };
    callbacks.onStrongholdConnect = []() {
        std::lock_guard<std::mutex> lock(g_ninjabrainClientMutex);
        g_ninjabrainClientStatus.MarkStrongholdConnected();
    };
    callbacks.onStrongholdDisconnect = [](const std::string& error) {
        {
            std::lock_guard<std::mutex> lock(g_ninjabrainClientMutex);
            g_ninjabrainClientStatus.MarkStrongholdDisconnected(error);
        }
        std::lock_guard<std::mutex> lock(g_ninjabrainDataMutex);
        ClearNinjabrainStrongholdData(g_ninjabrainData);
    };
    callbacks.onBoatMessage = [](const std::string& payload) {
        std::lock_guard<std::mutex> lock(g_ninjabrainDataMutex);
        ApplyNinjabrainBoatEvent(payload, g_ninjabrainData, LogNinjabrainMessage);
    };
    callbacks.onBoatConnect = []() {
        std::lock_guard<std::mutex> lock(g_ninjabrainClientMutex);
        g_ninjabrainClientStatus.MarkBoatConnected();
    };
    callbacks.onBoatDisconnect = [](const std::string& error) {
        {
            std::lock_guard<std::mutex> lock(g_ninjabrainClientMutex);
            g_ninjabrainClientStatus.MarkBoatDisconnected(error);
        }
        std::lock_guard<std::mutex> lock(g_ninjabrainDataMutex);
        ClearNinjabrainBoatData(g_ninjabrainData);
    };
    callbacks.onLog = LogNinjabrainMessage;

    try {
        g_ninjabrainClientSession = std::make_unique<NinjabrainApiSession>(apiBaseUrl, std::move(callbacks));
    } catch (...) {
        g_ninjabrainClientStatus.Stop();
        throw;
    }
}

void StopNinjabrainClient() {
    std::unique_ptr<NinjabrainApiSession> session;
    {
        std::lock_guard<std::mutex> lock(g_ninjabrainClientMutex);
        if (!g_ninjabrainClientSession) { return; }

        session = std::move(g_ninjabrainClientSession);
        g_ninjabrainClientStatus.Stop();
    }

    session->Stop();
}

void RestartNinjabrainClient() {
    std::unique_ptr<NinjabrainApiSession> session;
    {
        std::lock_guard<std::mutex> lock(g_ninjabrainClientMutex);
        session = std::move(g_ninjabrainClientSession);
        g_ninjabrainClientStatus.Stop();
    }

    if (session) { session->Stop(); }

    StartNinjabrainClient();
}

NinjabrainApiStatus GetNinjabrainClientStatus() {
    std::lock_guard<std::mutex> lock(g_ninjabrainClientMutex);
    return g_ninjabrainClientStatus.Snapshot();
}