#include "ninjabrain_data.h"

#include <atomic>
#include <mutex>

namespace {

std::atomic<std::shared_ptr<const NinjabrainData>> g_ninjabrainDataSnapshot{
	std::make_shared<const NinjabrainData>()
};
std::mutex g_ninjabrainDataWriteMutex;

} // namespace

std::shared_ptr<const NinjabrainData> GetNinjabrainDataSnapshot() {
	return g_ninjabrainDataSnapshot.load(std::memory_order_acquire);
}

void PublishNinjabrainData(NinjabrainData data) {
	std::lock_guard<std::mutex> lock(g_ninjabrainDataWriteMutex);
	g_ninjabrainDataSnapshot.store(std::make_shared<const NinjabrainData>(std::move(data)), std::memory_order_release);
}

void ModifyNinjabrainData(const std::function<void(NinjabrainData&)>& modifier) {
	std::lock_guard<std::mutex> lock(g_ninjabrainDataWriteMutex);

	const auto current = g_ninjabrainDataSnapshot.load(std::memory_order_acquire);
	auto next = std::make_shared<NinjabrainData>(current ? *current : NinjabrainData{});
	modifier(*next);

	g_ninjabrainDataSnapshot.store(std::shared_ptr<const NinjabrainData>(std::move(next)), std::memory_order_release);
}
