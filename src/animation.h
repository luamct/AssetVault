#pragma once

#include "3d.h"
#include "utils.h"
#include <assimp/scene.h>
#include <memory>
#include <vector>

// Parse animation clips from an Assimp scene and attach them to the model
void load_model_animations(const aiScene* scene, Model& model);

// Advance the active animation by delta_seconds and update bone transforms
void advance_model_animation(Model& model, float delta_seconds);

// Holds OpenGL textures and timing metadata for a 2D (GIF-style) animation.
struct Animation2D {
  std::vector<unsigned int> frame_textures;
  std::vector<int> frame_delays;
  std::vector<int> cumulative_frame_delays;
  int width;
  int height;
  int total_duration;

  Animation2D();
  ~Animation2D();

  void rebuild_timing_cache();
  bool empty() const;
  int frame_count() const;
  unsigned int frame_texture_at_time(int elapsed_ms) const;
};

// Lightweight playback controller that keeps per-view timing for a shared Animation2D.
struct Animation2DPlaybackState {
  std::shared_ptr<Animation2D> animation;
  TimePoint start_time;
  bool started;

  Animation2DPlaybackState();

  void set_animation(const std::shared_ptr<Animation2D>& new_animation,
    TimePoint now);
  void reset();
  unsigned int current_texture(TimePoint now) const;
  bool has_animation() const { return animation != nullptr; }
};
