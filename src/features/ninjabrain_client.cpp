#include "ninjabrain_client.h"

#include "features/ninjabrain_data.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "common/utils.h"

#include <cmath>
#include <functional>
#include <memory>
#include <stop_token>
#include <thread>

namespace {

using json = nlohmann::json;
using namespace std::chrono_literals;

constexpr auto kNinjabrainReconnectIntervalMs = 3000;
constexpr auto kNinjabrainConnectionTimeout = 3s;
constexpr char kStrongholdEventsPath[] = "/api/v1/stronghold/events";
constexpr char kBoatEventsPath[] = "/api/v1/boat/events";

std::mutex g_ninjabrainClientMutex;

std::string TrimString(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { return {}; }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

double NormalizeAngleDegrees(double angle) {
    while (angle > 180.0) { angle -= 360.0; }
    while (angle < -180.0) { angle += 360.0; }
    return angle;
}

void ClearStrongholdDataLocked() {
    const std::string boatState = g_ninjabrainData.boatState;
    const double boatAngle = g_ninjabrainData.boatAngle;
    const bool hasBoatAngle = g_ninjabrainData.hasBoatAngle;

    g_ninjabrainData = NinjabrainData{};
    g_ninjabrainData.boatState = boatState;
    g_ninjabrainData.boatAngle = boatAngle;
    g_ninjabrainData.hasBoatAngle = hasBoatAngle;
}

void ClearStrongholdData() {
    std::lock_guard<std::mutex> lock(g_ninjabrainDataMutex);
    ClearStrongholdDataLocked();
}

void ClearBoatData() {
    std::lock_guard<std::mutex> lock(g_ninjabrainDataMutex);
    g_ninjabrainData.boatState = "NONE";
    g_ninjabrainData.boatAngle = 0.0;
    g_ninjabrainData.hasBoatAngle = false;
}

void HandleBoatEvent(const std::string& payload) {
    if (payload.empty()) { return; }

    try {
        const json parsed = json::parse(payload);

        std::lock_guard<std::mutex> lock(g_ninjabrainDataMutex);
        g_ninjabrainData.boatState = parsed.value("boatState", "NONE");

        const auto boatAngle = parsed.find("boatAngle");
        if (boatAngle != parsed.end() && !boatAngle->is_null()) {
            g_ninjabrainData.boatAngle = boatAngle->get<double>();
            g_ninjabrainData.hasBoatAngle = true;
        } else {
            g_ninjabrainData.boatAngle = 0.0;
            g_ninjabrainData.hasBoatAngle = false;
        }
    } catch (const std::exception& exception) {
        LogCategory("ninjabrain", "Failed to parse boat event: " + std::string(exception.what()));
    }
}

void HandleStrongholdEvent(const std::string& payload) {
    if (payload.empty()) { return; }

    try {
        const json parsedJson = json::parse(payload);

        NinjabrainData previous;
        {
            std::lock_guard<std::mutex> lock(g_ninjabrainDataMutex);
            previous = g_ninjabrainData;
        }

        const std::string resultType = parsedJson.value("resultType", "NONE");
        if (resultType == "NONE" || resultType == "FAILED") {
            std::lock_guard<std::mutex> lock(g_ninjabrainDataMutex);
            ClearStrongholdDataLocked();
            g_ninjabrainData.resultType = resultType;
            return;
        }

        NinjabrainData next;
        next.resultType = resultType;

        if (const auto playerPosition = parsedJson.find("playerPosition");
            playerPosition != parsedJson.end() && playerPosition->is_object() && !playerPosition->empty()) {
            next.playerX = playerPosition->value("xInOverworld", 0.0);
            next.playerZ = playerPosition->value("zInOverworld", 0.0);
            next.playerInNether = playerPosition->value("isInNether", false);
            next.playerHorizontalAngle = playerPosition->value("horizontalAngle", 0.0);
            next.hasPlayerPos = true;
        }

        if (const auto eyeThrows = parsedJson.find("eyeThrows"); eyeThrows != parsedJson.end() && eyeThrows->is_array()) {
            next.eyeCount = (std::min)(static_cast<int>(eyeThrows->size()), static_cast<int>(next.throws.size()));
            for (int index = 0; index < next.eyeCount; ++index) {
                const auto& throwJson = (*eyeThrows)[index];
                auto& currentThrow = next.throws[index];
                currentThrow.angle = throwJson.value("angle", 0.0);
                currentThrow.angleWithoutCorrection = throwJson.value("angleWithoutCorrection", currentThrow.angle);
                currentThrow.correction = throwJson.value("correction", 0.0);
                currentThrow.type = throwJson.value("type", "NORMAL");

                const auto correctionIncrements = throwJson.find("correctionIncrements");
                if (correctionIncrements != throwJson.end() && !correctionIncrements->is_null()) {
                    currentThrow.correctionIncrements = correctionIncrements->get<int>();
                    currentThrow.hasCorrectionIncrements = true;
                }
            }
        }

        if (next.eyeCount > 0 && !next.throws[next.eyeCount - 1].hasCorrectionIncrements) {
            next.correctionIncrements151 = previous.correctionIncrements151;

            if (next.eyeCount != previous.eyeCount) {
                next.correctionIncrements151 = 0;
            } else {
                const double previousCorrection = previous.throws[previous.eyeCount - 1].correction;
                const double delta = next.throws[next.eyeCount - 1].correction - previousCorrection;
                if (delta > 1e-9) {
                    ++next.correctionIncrements151;
                } else if (delta < -1e-9) {
                    --next.correctionIncrements151;
                }
            }
        }

        if (next.eyeCount >= 1) {
            const auto& lastThrow = next.throws[next.eyeCount - 1];
            next.lastAngle = lastThrow.angle;
            next.lastAngleWithoutCorrection = lastThrow.angleWithoutCorrection;
            next.lastCorrection = lastThrow.correction;
            next.hasCorrection = std::abs(next.lastCorrection) > 1e-9;

            if (next.eyeCount >= 2) {
                next.prevAngle = next.throws[next.eyeCount - 2].angle;
                next.hasAngleChange = true;
                next.hasNetherAngle = true;
                next.netherAngle = next.lastAngle;
                next.netherAngleDiff = next.lastAngle - next.throws[0].angle;
            }
        }

        if (const auto predictions = parsedJson.find("predictions"); predictions != parsedJson.end() && predictions->is_array()) {
            next.predictionCount = (std::min)(static_cast<int>(predictions->size()), static_cast<int>(next.predictions.size()));
            for (int index = 0; index < next.predictionCount; ++index) {
                const auto& predictionJson = (*predictions)[index];
                auto& prediction = next.predictions[index];
                prediction.chunkX = predictionJson.value("chunkX", 0);
                prediction.chunkZ = predictionJson.value("chunkZ", 0);
                prediction.certainty = predictionJson.value("certainty", 0.0);
                prediction.overworldDistance = predictionJson.value("overworldDistance", 0.0);

                if (next.hasPlayerPos) {
                    constexpr double kPi = 3.14159265358979323846;
                    const double blockX = prediction.chunkX * 16.0 + 4.0;
                    const double blockZ = prediction.chunkZ * 16.0 + 4.0;
                    const double xDiff = blockX - next.playerX;
                    const double zDiff = blockZ - next.playerZ;
                    const double structureAngle = -std::atan2(xDiff, zDiff) * 180.0 / kPi;

                    next.predictionAngles[index].actualAngle = structureAngle;
                    next.predictionAngles[index].neededCorrection =
                        NormalizeAngleDegrees(structureAngle - next.playerHorizontalAngle);
                    next.predictionAngles[index].valid = true;
                }
            }
        }

        if (next.predictionCount > 0) {
            next.strongholdX = next.predictions[0].chunkX * 16 + 4;
            next.strongholdZ = next.predictions[0].chunkZ * 16 + 4;
            next.distance = next.predictions[0].overworldDistance;
            next.certainty = next.predictions[0].certainty;
            next.validPrediction = true;
        }

        std::lock_guard<std::mutex> lock(g_ninjabrainDataMutex);
        next.boatState = g_ninjabrainData.boatState;
        next.boatAngle = g_ninjabrainData.boatAngle;
        next.hasBoatAngle = g_ninjabrainData.hasBoatAngle;
        g_ninjabrainData = std::move(next);
    } catch (const std::exception& exception) {
        LogCategory("ninjabrain", "Failed to parse stronghold event: " + std::string(exception.what()));
    }
}

std::string ResolveApiBaseUrl() {
    std::string apiBaseUrl = ConfigDefaults::CONFIG_NINJABRAIN_API_BASE_URL;

    if (const auto configSnapshot = GetConfigSnapshot()) {
        apiBaseUrl = configSnapshot->ninjabrainOverlay.apiBaseUrl;
    } else {
        apiBaseUrl = g_config.ninjabrainOverlay.apiBaseUrl;
    }

    apiBaseUrl = TrimString(std::move(apiBaseUrl));
    while (!apiBaseUrl.empty() && apiBaseUrl.back() == '/') { apiBaseUrl.pop_back(); }
    if (apiBaseUrl.empty()) { apiBaseUrl = ConfigDefaults::CONFIG_NINJABRAIN_API_BASE_URL; }
    return apiBaseUrl;
}

class NinjabrainClientSession {
  public:
    explicit NinjabrainClientSession(std::string apiBaseUrl) : apiBaseUrl_(std::move(apiBaseUrl)) {
        strongholdThread_ = std::jthread([this](std::stop_token stopToken) {
            RunStream(stopToken, "stronghold", kStrongholdEventsPath, HandleStrongholdEvent, ClearStrongholdData);
        });
        boatThread_ = std::jthread([this](std::stop_token stopToken) {
            RunStream(stopToken, "boat", kBoatEventsPath, HandleBoatEvent, ClearBoatData);
        });
    }

    void Stop() {
        strongholdThread_.request_stop();
        boatThread_.request_stop();
    }

  private:
    void RunStream(std::stop_token stopToken,
                   const char* streamName,
                   const char* path,
                   const std::function<void(const std::string&)>& onMessage,
                   const std::function<void()>& onDisconnect) const {
        httplib::Client client(apiBaseUrl_);
        client.set_keep_alive(true);
        client.set_connection_timeout(
            static_cast<time_t>(std::chrono::duration_cast<std::chrono::seconds>(kNinjabrainConnectionTimeout).count()), 0);

        const httplib::Headers headers = {
            { "Accept", "text/event-stream" },
            { "Cache-Control", "no-cache" },
        };

        httplib::sse::SSEClient sse(client, path, headers);
        sse.set_reconnect_interval(kNinjabrainReconnectIntervalMs);

        bool disconnected = false;
        sse.on_open([&]() {
            if (disconnected) {
                LogCategory("ninjabrain", std::string("Reconnected ") + streamName + " stream.");
                disconnected = false;
            }
        });
        sse.on_message([&](const httplib::sse::SSEMessage& message) { onMessage(message.data); });
        sse.on_error([&](httplib::Error error) {
            if (stopToken.stop_requested() || error == httplib::Error::Canceled) { return; }

            onDisconnect();
            if (!disconnected) {
                LogCategory(
                    "ninjabrain",
                    std::string("Lost ") + streamName + " stream: " + httplib::to_string(error) + ". Waiting for reconnect.");
                disconnected = true;
            }
        });

        std::stop_callback stopCallback(stopToken, [&]() {
            sse.stop();
            client.stop();
        });

        sse.start();
    }

    std::string apiBaseUrl_;
    std::jthread strongholdThread_;
    std::jthread boatThread_;
};

std::unique_ptr<NinjabrainClientSession> g_ninjabrainClientSession;

} // namespace

void StartNinjabrainClient() {
    std::lock_guard<std::mutex> lock(g_ninjabrainClientMutex);
    if (g_ninjabrainClientSession) { return; }

    {
        std::lock_guard<std::mutex> dataLock(g_ninjabrainDataMutex);
        g_ninjabrainData = NinjabrainData{};
    }

    g_ninjabrainClientSession = std::make_unique<NinjabrainClientSession>(ResolveApiBaseUrl());
}

void StopNinjabrainClient() {
    std::unique_ptr<NinjabrainClientSession> session;
    {
        std::lock_guard<std::mutex> lock(g_ninjabrainClientMutex);
        if (!g_ninjabrainClientSession) { return; }

        session = std::move(g_ninjabrainClientSession);
    }

    session->Stop();
}