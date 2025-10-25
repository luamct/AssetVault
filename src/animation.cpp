#include "animation.h"
#include "logger.h"
#include "config.h"
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

namespace {

double sanitize_ticks_per_second(double ticks_per_second) {
  return (ticks_per_second > 0.0) ? ticks_per_second : 25.0;
}

size_t find_key_index(const std::vector<AnimationKeyframeVec3>& keys, double time_ticks) {
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    if (time_ticks < keys[i + 1].time) {
      return i;
    }
  }
  return keys.size() - 2; // Safe because caller checks size >= 2
}

size_t find_key_index(const std::vector<AnimationKeyframeQuat>& keys, double time_ticks) {
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    if (time_ticks < keys[i + 1].time) {
      return i;
    }
  }
  return keys.size() - 2;
}

glm::vec3 interpolate_vec3(const std::vector<AnimationKeyframeVec3>& keys, double time_ticks, const glm::vec3& fallback) {
  if (keys.empty()) {
    return fallback;
  }

  if (keys.size() == 1) {
    return keys.front().value;
  }

  size_t index = find_key_index(keys, time_ticks);
  const AnimationKeyframeVec3& start = keys[index];
  const AnimationKeyframeVec3& end = keys[index + 1];
  double span = end.time - start.time;
  double factor = (span > 0.0) ? (time_ticks - start.time) / span : 0.0;
  factor = std::clamp(factor, 0.0, 1.0);
  return glm::mix(start.value, end.value, static_cast<float>(factor));
}

glm::quat interpolate_quat(const std::vector<AnimationKeyframeQuat>& keys, double time_ticks, const glm::quat& fallback) {
  if (keys.empty()) {
    return fallback;
  }

  if (keys.size() == 1) {
    return glm::normalize(keys.front().value);
  }

  size_t index = find_key_index(keys, time_ticks);
  const AnimationKeyframeQuat& start = keys[index];
  const AnimationKeyframeQuat& end = keys[index + 1];
  double span = end.time - start.time;
  double factor = (span > 0.0) ? (time_ticks - start.time) / span : 0.0;
  factor = std::clamp(factor, 0.0, 1.0);
  return glm::normalize(glm::slerp(start.value, end.value, static_cast<float>(factor)));
}

} // namespace

// Convert Assimp animation clips into our runtime format and start playback when available.
void load_model_animations(const aiScene* scene, Model& model) {
  model.animations.clear();
  model.animation_playing = false;
  model.animation_time = 0.0;
  model.active_animation = 0;

  if (!scene || scene->mNumAnimations == 0 || model.bones.empty() || !Config::PREVIEW_PLAY_ANIMATIONS) {
    return;
  }

  if (model.skeleton_nodes.empty()) {
    LOG_WARN("[ANIMATION] Scene has animations but skeleton nodes were not initialized");
    return;
  }

  model.animations.reserve(scene->mNumAnimations);
  LOG_DEBUG("[ANIMATION] Building clips for scene with {} animations and {} skeleton nodes", scene->mNumAnimations, model.skeleton_nodes.size());

  for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
    const aiAnimation* ai_anim = scene->mAnimations[a];
    if (!ai_anim) {
      continue;
    }

    AnimationClip clip;
    clip.name = ai_anim->mName.length > 0 ? ai_anim->mName.C_Str() : "Unnamed";
    clip.duration = ai_anim->mDuration;
    clip.ticks_per_second = sanitize_ticks_per_second(ai_anim->mTicksPerSecond);

    double max_key_time = 0.0;

    clip.channels.reserve(ai_anim->mNumChannels);
    std::unordered_map<int, size_t> channel_lookup;
    LOG_DEBUG("[ANIMATION] Clip '{}' original duration {} ticks @ {} tps (channels={})", clip.name, clip.duration, clip.ticks_per_second, ai_anim->mNumChannels);
    for (unsigned int c = 0; c < ai_anim->mNumChannels; ++c) {
      const aiNodeAnim* channel = ai_anim->mChannels[c];
      if (!channel) {
        continue;
      }

      std::string raw_name = channel->mNodeName.C_Str();
      auto node_it = model.skeleton_node_lookup.find(raw_name);
      if (node_it == model.skeleton_node_lookup.end()) {
        LOG_WARN("[ANIMATION] Channel '{}' has no matching skeleton node", raw_name);
        continue;
      }

      int node_index = node_it->second;
      size_t channel_index;
      auto existing = channel_lookup.find(node_index);
      if (existing == channel_lookup.end()) {
        channel_index = clip.channels.size();
        AnimationChannel anim_channel;
        anim_channel.node_index = node_index;
        clip.channels.push_back(std::move(anim_channel));
        channel_lookup[node_index] = channel_index;
        LOG_DEBUG("[ANIMATION] New channel for node '{}' (index {})", raw_name, node_index);
      }
      else {
        channel_index = existing->second;
        LOG_DEBUG("[ANIMATION] Merging additional channel data into node '{}' (index {})", raw_name, node_index);
      }

      AnimationChannel& anim_channel = clip.channels[channel_index];

      anim_channel.position_keys.reserve(anim_channel.position_keys.size() + channel->mNumPositionKeys);
      for (unsigned int i = 0; i < channel->mNumPositionKeys; ++i) {
        const aiVectorKey& key = channel->mPositionKeys[i];
        AnimationKeyframeVec3 frame;
        frame.time = key.mTime;
        frame.value = glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z);
        anim_channel.position_keys.push_back(frame);
        max_key_time = std::max(max_key_time, frame.time);
      }

      anim_channel.rotation_keys.reserve(anim_channel.rotation_keys.size() + channel->mNumRotationKeys);
      for (unsigned int i = 0; i < channel->mNumRotationKeys; ++i) {
        const aiQuatKey& key = channel->mRotationKeys[i];
        AnimationKeyframeQuat frame;
        frame.time = key.mTime;
        frame.value = glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z);
        anim_channel.rotation_keys.push_back(frame);
        max_key_time = std::max(max_key_time, frame.time);
      }

      anim_channel.scaling_keys.reserve(anim_channel.scaling_keys.size() + channel->mNumScalingKeys);
      for (unsigned int i = 0; i < channel->mNumScalingKeys; ++i) {
        const aiVectorKey& key = channel->mScalingKeys[i];
        AnimationKeyframeVec3 frame;
        frame.time = key.mTime;
        frame.value = glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z);
        anim_channel.scaling_keys.push_back(frame);
        max_key_time = std::max(max_key_time, frame.time);
      }
    }

    if (!clip.channels.empty()) {
      for (AnimationChannel& channel : clip.channels) {
        auto sort_vec3 = [](std::vector<AnimationKeyframeVec3>& keys) {
          std::sort(keys.begin(), keys.end(), [](const AnimationKeyframeVec3& a, const AnimationKeyframeVec3& b) {
            return a.time < b.time;
          });
        };
        auto sort_quat = [](std::vector<AnimationKeyframeQuat>& keys) {
          std::sort(keys.begin(), keys.end(), [](const AnimationKeyframeQuat& a, const AnimationKeyframeQuat& b) {
            return a.time < b.time;
          });
        };
        sort_vec3(channel.position_keys);
        sort_quat(channel.rotation_keys);
        sort_vec3(channel.scaling_keys);
      }

      if (max_key_time > 0.0) {
        clip.duration = std::max(clip.duration, max_key_time);
      }
      model.animations.push_back(std::move(clip));
      LOG_DEBUG("[ANIMATION] Clip '{}' registered with {} channels, duration {:.3f}s", model.animations.back().name, model.animations.back().channels.size(), model.animations.back().duration / model.animations.back().ticks_per_second);
    }
  }

  if (!model.animations.empty()) {
    model.animation_playing = true;
    model.animation_time = 0.0;
    model.active_animation = model.animations.size() - 1;
    model.animated_local_transforms.resize(model.bones.size(), glm::mat4(1.0f));
    model.animated_node_local_transforms.resize(model.skeleton_nodes.size(), glm::mat4(1.0f));
    model.animated_node_global_transforms.resize(model.skeleton_nodes.size(), glm::mat4(1.0f));
    LOG_DEBUG("[ANIMATION] Loaded {} animation clip(s)", model.animations.size());
  }
}

// Step the active clip forward and recompute bone globals, writing results into the model.
void advance_model_animation(Model& model, float delta_seconds) {
  if (!Config::PREVIEW_PLAY_ANIMATIONS || !model.animation_playing || model.animations.empty() || model.bones.empty()) {
    return;
  }

  if (model.active_animation >= model.animations.size()) {
    model.active_animation = model.animations.size() - 1;
  }

  AnimationClip& clip = model.animations[model.active_animation];
  if (!clip.is_valid()) {
    return;
  }

  const double ticks_per_second = sanitize_ticks_per_second(clip.ticks_per_second);
  const double duration_ticks = (clip.duration > 0.0) ? clip.duration : 0.0;
  if (duration_ticks <= 0.0) {
    return;
  }

  model.animation_time += static_cast<double>(delta_seconds);
  double time_in_ticks = std::fmod(model.animation_time * ticks_per_second, duration_ticks);
  if (time_in_ticks < 0.0) {
    time_in_ticks += duration_ticks;
  }

  static int debug_frames = 0;
  if (debug_frames < 10) {
    LOG_TRACE("[ANIMATION] Advancing '{}' to {:.3f}s (ticks {:.3f}/{:.3f})", clip.name, model.animation_time, time_in_ticks, duration_ticks);
  }

  const size_t node_count = model.skeleton_nodes.size();
  if (node_count == 0) {
    return;
  }

  if (model.animated_node_local_transforms.size() != node_count) {
    model.animated_node_local_transforms.assign(node_count, glm::mat4(1.0f));
  }
  if (model.animated_node_global_transforms.size() != node_count) {
    model.animated_node_global_transforms.assign(node_count, glm::mat4(1.0f));
  }

  for (size_t i = 0; i < node_count; ++i) {
    model.animated_node_local_transforms[i] = model.skeleton_nodes[i].rest_local_transform;
  }

  for (const AnimationChannel& channel : clip.channels) {
    if (channel.node_index < 0 || static_cast<size_t>(channel.node_index) >= node_count) {
      continue;
    }

    const SkeletonNode& node_def = model.skeleton_nodes[channel.node_index];
    glm::vec3 translation = interpolate_vec3(channel.position_keys, time_in_ticks, node_def.rest_position);
    glm::quat rotation = interpolate_quat(channel.rotation_keys, time_in_ticks, node_def.rest_rotation);
    glm::vec3 scale = interpolate_vec3(channel.scaling_keys, time_in_ticks, node_def.rest_scale);

    if (debug_frames < 10) {
      LOG_TRACE("[ANIMATION] Node '{}' idx {} -> T({}, {}, {}), R({}, {}, {}, {}), S({}, {}, {})",
        node_def.name_raw, channel.node_index,
        translation.x, translation.y, translation.z,
        rotation.w, rotation.x, rotation.y, rotation.z,
        scale.x, scale.y, scale.z);
    }

    glm::mat4 translation_m = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 rotation_m = glm::toMat4(rotation);
    glm::mat4 scaling_m = glm::scale(glm::mat4(1.0f), scale);
    model.animated_node_local_transforms[channel.node_index] = translation_m * rotation_m * scaling_m;
  }

  const size_t bone_count = model.bones.size();
  if (model.animated_local_transforms.size() != bone_count) {
    model.animated_local_transforms.assign(bone_count, glm::mat4(1.0f));
  }

  std::function<void(int, const glm::mat4&)> update_global = [&](int node_index, const glm::mat4& parent_global) {
    if (node_index < 0 || static_cast<size_t>(node_index) >= node_count) {
      return;
    }

    glm::mat4 local = model.animated_node_local_transforms[node_index];
    glm::mat4 global = parent_global * local;
    model.animated_node_global_transforms[node_index] = global;

    const SkeletonNode& node = model.skeleton_nodes[node_index];
    if (node.is_bone && node.bone_index >= 0 && static_cast<size_t>(node.bone_index) < bone_count) {
      Bone& bone = model.bones[node.bone_index];
      bone.local_transform = local;
      bone.global_transform = global;
      model.animated_local_transforms[node.bone_index] = local;
    }

    for (int child_index : node.child_indices) {
      update_global(child_index, global);
    }
  };

  for (size_t i = 0; i < node_count; ++i) {
    if (model.skeleton_nodes[i].parent_index == -1) {
      update_global(static_cast<int>(i), glm::mat4(1.0f));
    }
  }

  if (debug_frames < 10) {
    debug_frames++;
  }
}
