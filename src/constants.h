#ifndef GODOTFMOD_CONSTANTS_H
#define GODOTFMOD_CONSTANTS_H

static constexpr const char* FMOD_SETTINGS_BASE_PATH = "Fmod";
static constexpr const char* FMOD_SETTING_AUTO_INITIALIZE = "auto_initialize";

static constexpr const bool DEFAULT_AUTO_INITIALIZE = true;

// Web output: mix ahead into a queue read by the extension's AudioWorklet (see fmod_web_output.h).
static constexpr const char* FMOD_WEB_SETTINGS_BASE_PATH = "Web";
static constexpr const char* FMOD_WEB_SETTING_QUEUED_OUTPUT = "queued_output";
static constexpr const char* FMOD_WEB_SETTING_QUEUE_MS = "queue_ms";
static constexpr const bool DEFAULT_WEB_QUEUED_OUTPUT = true;
static constexpr const int DEFAULT_WEB_QUEUE_MS = 200;

#endif// GODOTFMOD_CONSTANTS_H
