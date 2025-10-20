#pragma once

#include "3d.h"
#include <assimp/scene.h>

// Parse animation clips from an Assimp scene and attach them to the model
void load_model_animations(const aiScene* scene, Model& model);

// Advance the active animation by delta_seconds and update bone transforms
void advance_model_animation(Model& model, float delta_seconds);

