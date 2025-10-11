#include "audio_manager.h"
#include "logger.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#include <objbase.h>
#undef MA_COINIT_VALUE
#define MA_COINIT_VALUE COINIT_APARTMENTTHREADED
#endif

// Enable OGG/Vorbis support with stb_vorbis
// Include stb_vorbis header so miniaudio can detect it
#define STB_VORBIS_HEADER_ONLY
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4701) // potentially uninitialized local variable
#endif
#include "miniaudio/stb_vorbis.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Include miniaudio implementation - it will detect stb_vorbis is available
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

AudioManager::AudioManager()
  : engine_(nullptr)
  , current_sound_(nullptr)
  , initialized_(false)
  , sound_loaded_(false) {
}

AudioManager::~AudioManager() {
  cleanup();
}

bool AudioManager::initialize() {
  if (initialized_) {
    return true;
  }

  // Skip audio initialization in headless/test mode (no audio device available)
  if (std::getenv("TESTING")) {
    LOG_INFO("Skipping audio initialization in test mode (no audio device required)");
    return true; // Return success but don't actually initialize
  }

  // Allocate engine
  engine_ = new ma_engine;

  // Create engine config with better buffer settings to prevent crackling
  ma_engine_config engineConfig = ma_engine_config_init();

  // Increase buffer size to prevent crackling (2048 frames is a good balance)
  engineConfig.periodSizeInFrames = 2048;
  engineConfig.periodSizeInMilliseconds = 0; // Use frames instead

  // Use stereo output
  engineConfig.channels = 2;

  // Use default sample rate (usually 44100 or 48000)
  engineConfig.sampleRate = 0; // 0 = use device default

  // Initialize miniaudio engine with custom config
  ma_result result = ma_engine_init(&engineConfig, engine_);
  if (result != MA_SUCCESS) {
    LOG_ERROR("Failed to initialize audio engine. Error: {}", static_cast<int>(result));
    delete engine_;
    engine_ = nullptr;
    return false;
  }

  initialized_ = true;
  return true;
}

void AudioManager::cleanup() {
  if (!initialized_) {
    return;
  }

  // Unload any loaded audio
  unload_audio();

  // Cleanup engine
  if (engine_) {
    ma_engine_uninit(engine_);
    delete engine_;
    engine_ = nullptr;
  }

  initialized_ = false;
  LOG_INFO("Audio system cleaned up");
}

bool AudioManager::load_audio(const std::string& filepath) {
  if (!initialized_) {
    LOG_ERROR("Audio system not initialized");
    return false;
  }

  // Unload previous audio if any
  unload_audio();

  // Allocate sound
  current_sound_ = new ma_sound;

  // Use flags for better performance, but not streaming for OGG files to allow duration detection
  ma_uint32 flags = MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION;

  // Check if this is an OGG file - if so, don't use streaming to allow duration detection
  std::string extension = filepath.substr(filepath.find_last_of('.'));
  std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

  // Use streaming for larger files except OGG where we need duration
  if (extension != ".ogg") {
    flags |= MA_SOUND_FLAG_STREAM;
  }

  // Load the sound file (streaming disabled for OGG to allow duration detection)
  ma_result result = ma_sound_init_from_file(engine_, filepath.c_str(), flags, NULL, NULL, current_sound_);
  if (result != MA_SUCCESS) {
    LOG_ERROR("Failed to load audio file: {}. Error: {}", filepath, static_cast<int>(result));
    delete current_sound_;
    current_sound_ = nullptr;
    return false;
  }

  current_file_ = filepath;
  sound_loaded_ = true;
  LOG_INFO("Loaded audio file: {}", filepath);
  return true;
}

void AudioManager::unload_audio() {
  if (!sound_loaded_ || !current_sound_) {
    return;
  }

  // Stop playback if playing
  stop();

  // Uninitialize and delete sound
  ma_sound_uninit(current_sound_);
  delete current_sound_;
  current_sound_ = nullptr;

  current_file_.clear();
  sound_loaded_ = false;
}

void AudioManager::play() {
  if (!sound_loaded_ || !current_sound_) {
    return;
  }

  ma_sound_start(current_sound_);
}

void AudioManager::pause() {
  if (!sound_loaded_ || !current_sound_) {
    return;
  }

  ma_sound_stop(current_sound_);
}

void AudioManager::stop() {
  if (!sound_loaded_ || !current_sound_) {
    return;
  }

  ma_sound_stop(current_sound_);
  ma_sound_seek_to_pcm_frame(current_sound_, 0);
}

bool AudioManager::is_playing() const {
  if (!sound_loaded_ || !current_sound_) {
    return false;
  }

  return ma_sound_is_playing(current_sound_);
}

void AudioManager::set_volume(float volume) {
  if (!sound_loaded_ || !current_sound_) {
    return;
  }

  // Clamp volume to 0.0 - 1.0
  if (volume < 0.0f) volume = 0.0f;
  if (volume > 1.0f) volume = 1.0f;

  ma_sound_set_volume(current_sound_, volume);
}

float AudioManager::get_volume() const {
  if (!sound_loaded_ || !current_sound_) {
    return 1.0f;
  }

  return ma_sound_get_volume(current_sound_);
}

float AudioManager::get_duration() const {
  if (!sound_loaded_ || !current_sound_) {
    return 0.0f;
  }

  // Get length in PCM frames
  ma_uint64 length_in_frames;
  ma_result result = ma_sound_get_length_in_pcm_frames(current_sound_, &length_in_frames);
  if (result != MA_SUCCESS) {
    return 0.0f;
  }

  // Get sample rate
  ma_uint32 sample_rate = ma_engine_get_sample_rate(engine_);

  // Convert frames to seconds
  return static_cast<float>(length_in_frames) / static_cast<float>(sample_rate);
}

float AudioManager::get_position() const {
  if (!sound_loaded_ || !current_sound_) {
    return 0.0f;
  }

  // Get cursor in PCM frames
  ma_uint64 cursor_in_frames;
  ma_result result = ma_sound_get_cursor_in_pcm_frames(current_sound_, &cursor_in_frames);
  if (result != MA_SUCCESS) {
    return 0.0f;
  }

  // Get sample rate
  ma_uint32 sample_rate = ma_engine_get_sample_rate(engine_);

  // Convert frames to seconds
  return static_cast<float>(cursor_in_frames) / static_cast<float>(sample_rate);
}

void AudioManager::set_position(float seconds) {
  if (!sound_loaded_ || !current_sound_) {
    return;
  }

  // Get sample rate
  ma_uint32 sample_rate = ma_engine_get_sample_rate(engine_);

  // Convert seconds to PCM frames
  ma_uint64 target_frame = static_cast<ma_uint64>(seconds * sample_rate);

  // Seek to position
  ma_sound_seek_to_pcm_frame(current_sound_, target_frame);
}
