#pragma once

#include <string>
#include <atomic>

// Forward declare miniaudio types to avoid including the header here
typedef struct ma_engine ma_engine;
typedef struct ma_sound ma_sound;

class AudioManager {
public:
  AudioManager();
  ~AudioManager();

  // Initialize/cleanup
  bool initialize();
  void cleanup();
  bool is_initialized() const { return initialized_; }

  // Audio loading
  bool load_audio(const std::string& filepath);
  void unload_audio();

  // Playback controls
  void play();
  void pause();
  void stop();
  bool is_playing() const;

  // Volume control (0.0 to 1.0)
  void set_volume(float volume);
  float get_volume() const;

  // Playback info
  float get_duration() const;
  float get_position() const;
  void set_position(float seconds);

  // Audio file info
  const std::string& get_current_file() const { return current_file_; }
  bool has_audio_loaded() const { return sound_loaded_; }

private:
  ma_engine* engine_;
  ma_sound* current_sound_;
  
  std::atomic<bool> initialized_;
  std::atomic<bool> sound_loaded_;
  std::string current_file_;
  
  // Prevent copying
  AudioManager(const AudioManager&) = delete;
  AudioManager& operator=(const AudioManager&) = delete;
};