#include "animation.h"
#include "logger.h"
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <functional>

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

  if (!scene || scene->mNumAnimations == 0 || model.bones.empty()) {
    return;
  }

  model.animations.reserve(scene->mNumAnimations);

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
    for (unsigned int c = 0; c < ai_anim->mNumChannels; ++c) {
      const aiNodeAnim* channel = ai_anim->mChannels[c];
      if (!channel) {
        continue;
      }

      std::string bone_name = channel->mNodeName.C_Str();
      auto bone_it = model.bone_lookup.find(bone_name);
      if (bone_it == model.bone_lookup.end()) {
        continue; // Animation affects a node we are not rendering
      }

      AnimationChannel anim_channel;
      anim_channel.bone_index = bone_it->second;

      anim_channel.position_keys.reserve(channel->mNumPositionKeys);
      for (unsigned int i = 0; i < channel->mNumPositionKeys; ++i) {
        const aiVectorKey& key = channel->mPositionKeys[i];
        AnimationKeyframeVec3 frame;
        frame.time = key.mTime;
        frame.value = glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z);
        anim_channel.position_keys.push_back(frame);
        max_key_time = std::max(max_key_time, frame.time);
      }

      anim_channel.rotation_keys.reserve(channel->mNumRotationKeys);
      for (unsigned int i = 0; i < channel->mNumRotationKeys; ++i) {
        const aiQuatKey& key = channel->mRotationKeys[i];
        AnimationKeyframeQuat frame;
        frame.time = key.mTime;
        frame.value = glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z);
        anim_channel.rotation_keys.push_back(frame);
        max_key_time = std::max(max_key_time, frame.time);
      }

      anim_channel.scaling_keys.reserve(channel->mNumScalingKeys);
      for (unsigned int i = 0; i < channel->mNumScalingKeys; ++i) {
        const aiVectorKey& key = channel->mScalingKeys[i];
        AnimationKeyframeVec3 frame;
        frame.time = key.mTime;
        frame.value = glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z);
        anim_channel.scaling_keys.push_back(frame);
        max_key_time = std::max(max_key_time, frame.time);
      }

      clip.channels.push_back(std::move(anim_channel));
    }

    if (!clip.channels.empty()) {
      if (max_key_time > 0.0) {
        clip.duration = std::max(clip.duration, max_key_time);
      }
      model.animations.push_back(std::move(clip));
    }
  }

  if (!model.animations.empty()) {
    model.animation_playing = true;
    model.animation_time = 0.0;
    model.active_animation = model.animations.size() - 1;
    model.animated_local_transforms.resize(model.bones.size(), glm::mat4(1.0f));
    LOG_INFO("[ANIMATION] Loaded {} animation clip(s)", model.animations.size());
  }
}

// Step the active clip forward and recompute bone globals, writing results into the model.
void advance_model_animation(Model& model, float delta_seconds) {
  if (!model.animation_playing || model.animations.empty() || model.bones.empty()) {
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

  const size_t bone_count = model.bones.size();
  if (model.animated_local_transforms.size() != bone_count) {
    model.animated_local_transforms.resize(bone_count, glm::mat4(1.0f));
  }

  for (size_t i = 0; i < bone_count; ++i) {
    model.animated_local_transforms[i] = model.bones[i].local_transform;
  }

  for (const AnimationChannel& channel : clip.channels) {
    if (channel.bone_index < 0 || static_cast<size_t>(channel.bone_index) >= bone_count) {
      continue;
    }

    const Bone& bone = model.bones[channel.bone_index];
    glm::vec3 translation = interpolate_vec3(channel.position_keys, time_in_ticks, bone.rest_position);
    glm::quat rotation = interpolate_quat(channel.rotation_keys, time_in_ticks, bone.rest_rotation);
    glm::vec3 scale = interpolate_vec3(channel.scaling_keys, time_in_ticks, bone.rest_scale);

    glm::mat4 translation_m = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 rotation_m = glm::toMat4(rotation);
    glm::mat4 scaling_m = glm::scale(glm::mat4(1.0f), scale);
    model.animated_local_transforms[channel.bone_index] = translation_m * rotation_m * scaling_m;
  }

  std::function<void(int, const glm::mat4&)> update_global = [&](int index, const glm::mat4& parent_transform) {
    if (index < 0 || static_cast<size_t>(index) >= bone_count) {
      return;
    }

    glm::mat4 global = parent_transform * model.animated_local_transforms[index];
    model.bones[index].global_transform = global;

    for (int child : model.bones[index].child_indices) {
      update_global(child, global);
    }
  };

  const glm::mat4 identity = glm::mat4(1.0f);
  for (size_t i = 0; i < bone_count; ++i) {
    if (model.bones[i].parent_index == -1) {
      update_global(static_cast<int>(i), identity);
    }
  }
}
