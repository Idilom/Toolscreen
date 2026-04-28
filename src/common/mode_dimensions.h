#pragma once

struct Config;
struct ModeConfig;

void RecalculateModeDimensions();
void RecalculateModeDimensions(Config& config, int screenW, int screenH);
bool SyncPreemptiveModeFromEyeZoom(Config& config);
int ResolveModeDisplayWidth(const ModeConfig& mode, int screenW, int screenH);
int ResolveModeDisplayHeight(const ModeConfig& mode, int screenW, int screenH);