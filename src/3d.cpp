#include "3d.h"
#include "logger.h"
#include "animation.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include "stb_image.h"
#include "texture_manager.h"
#include "theme.h"
#include "utils.h"

// 3D Preview global variables are now managed by TextureManager

// 3D Preview no longer needs a global current model
// Model state is now managed by the caller

// Unified shader program for all 3D rendering (loaded from external files)
static unsigned int shader_ = 0;

namespace {

constexpr int VERTEX_FLOAT_STRIDE = MODEL_VERTEX_FLOAT_STRIDE;
constexpr int POSITION_OFFSET = 0;
constexpr int NORMAL_OFFSET = 3;
constexpr int TEXCOORD_OFFSET = 6;
constexpr int BONE_ID_OFFSET = 8;
constexpr int BONE_WEIGHT_OFFSET = 12;
constexpr int MAX_SHADER_BONES = 128;

std::string normalize_node_name(const std::string& name) {
  std::string clean = name;

  auto ns_pos = clean.find(':');
  if (ns_pos != std::string::npos) {
    clean = clean.substr(ns_pos + 1);
  }

  const std::string helper_tag = "_$AssimpFbx$_";
  auto helper_pos = clean.find(helper_tag);
  if (helper_pos != std::string::npos) {
    clean = clean.substr(0, helper_pos);
  }

  return clean;
}

// Return a normalized direction vector that mimics a headlamp mounted on the camera.
glm::vec3 compute_preview_light_direction(const glm::vec3& camera_position) {
  glm::vec3 direction = -camera_position;
  if (glm::length(direction) < 0.0001f) {
    direction = glm::vec3(0.0f, -1.0f, -1.0f);
  }
  return glm::normalize(direction);
}

// Utility: unpack a matrix into T/R/S components while falling back to identity on failure.
bool decompose_transform(const glm::mat4& matrix, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale) {
  glm::vec3 skew;
  glm::vec4 perspective;
  if (!glm::decompose(matrix, scale, rotation, translation, skew, perspective)) {
    translation = glm::vec3(0.0f);
    rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    scale = glm::vec3(1.0f);
    return false;
  }

  rotation = glm::normalize(rotation);
  return true;
}

}

// Helper function to get base path
// Extract directory portion of a path so relative texture lookups work.
std::string getBasePath(const std::string& path) {
  size_t pos = path.find_last_of("\\/");
  return (std::string::npos == pos) ? "" : path.substr(0, pos + 1);
}


// Load all materials from model
// Populate the model's material list and load any referenced textures (embedded or external).
void load_model_materials(const aiScene* scene, const std::string& model_path, Model& model, TextureManager& texture_manager) {
  LOG_TRACE("[MATERIAL] Loading materials for model: {}", model_path);
  model.materials.clear();

  if (!scene || scene->mNumMaterials == 0) {
    LOG_WARN("[MATERIAL] No materials found in model");
    return;
  }

  // Get base path like the Assimp sample does
  std::string basepath = getBasePath(model_path);
  LOG_TRACE("[MATERIAL] Base path for textures: {}", basepath);

  // Log embedded texture information
  LOG_TRACE("[EMBEDDED] Scene contains {} embedded textures", scene->mNumTextures);
  for (unsigned int i = 0; i < scene->mNumTextures; i++) {
    const aiTexture* ai_tex = scene->mTextures[i];
    if (ai_tex) {
      LOG_TRACE("[EMBEDDED] Texture {}: {}x{}, format: '{}', filename: '{}'",
                i, ai_tex->mWidth, ai_tex->mHeight, ai_tex->achFormatHint,
                ai_tex->mFilename.length > 0 ? ai_tex->mFilename.C_Str() : "<no filename>");
    }
  }

  // Process ALL materials
  for (unsigned int m = 0; m < scene->mNumMaterials; m++) {
    aiMaterial* ai_material = scene->mMaterials[m];
    Material material;

    // Get material name
    aiString material_name;
    if (ai_material->Get(AI_MATKEY_NAME, material_name) == AI_SUCCESS) {
      material.name = material_name.C_Str();
    }
    else {
      material.name = "Material_" + std::to_string(m);
    }

    LOG_TRACE("[MATERIAL] Processing material {}: '{}'", m, material.name);

    // Debug: Check all texture types this material has
    LOG_TRACE("[MATERIAL] === Texture inventory for material '{}' ===", material.name);
    LOG_TRACE("[MATERIAL]   Diffuse textures: {}", ai_material->GetTextureCount(aiTextureType_DIFFUSE));
    LOG_TRACE("[MATERIAL]   Normal textures: {}", ai_material->GetTextureCount(aiTextureType_NORMALS));
    LOG_TRACE("[MATERIAL]   Specular textures: {}", ai_material->GetTextureCount(aiTextureType_SPECULAR));
    LOG_TRACE("[MATERIAL]   Emissive textures: {}", ai_material->GetTextureCount(aiTextureType_EMISSIVE));
    LOG_TRACE("[MATERIAL]   Metallic textures: {}", ai_material->GetTextureCount(aiTextureType_METALNESS));
    LOG_TRACE("[MATERIAL]   Roughness textures: {}", ai_material->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS));
    LOG_TRACE("[MATERIAL]   Ambient textures: {}", ai_material->GetTextureCount(aiTextureType_AMBIENT));
    LOG_TRACE("[MATERIAL]   Height/Bump textures: {}", ai_material->GetTextureCount(aiTextureType_HEIGHT));
    LOG_TRACE("[MATERIAL]   Reflection textures: {}", ai_material->GetTextureCount(aiTextureType_REFLECTION));

    // Debug: Check material properties
    aiColor3D emissive_color;
    float emissive_intensity = 0.0f;
    float metallic_factor = 0.0f;
    float roughness_factor = 0.5f;

    if (ai_material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive_color) == AI_SUCCESS) {
      LOG_TRACE("[MATERIAL]   Emissive color: ({:.3f}, {:.3f}, {:.3f})",
        emissive_color.r, emissive_color.g, emissive_color.b);
    }
    if (ai_material->Get(AI_MATKEY_EMISSIVE_INTENSITY, emissive_intensity) == AI_SUCCESS) {
      LOG_TRACE("[MATERIAL]   Emissive intensity: {:.3f}", emissive_intensity);
    }
    if (ai_material->Get(AI_MATKEY_METALLIC_FACTOR, metallic_factor) == AI_SUCCESS) {
      LOG_TRACE("[MATERIAL]   Metallic factor: {:.3f}", metallic_factor);
    }
    if (ai_material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness_factor) == AI_SUCCESS) {
      LOG_TRACE("[MATERIAL]   Roughness factor: {:.3f}", roughness_factor);
    }

    // Check for diffuse textures first
    unsigned int diffuse_count = ai_material->GetTextureCount(aiTextureType_DIFFUSE);
    LOG_TRACE("[MATERIAL] Material '{}' has {} diffuse textures", material.name, diffuse_count);

    // Try to load texture - atomic approach
    for (unsigned int texIndex = 0; texIndex < diffuse_count; texIndex++) {
      aiString texture_path;
      aiReturn texFound = ai_material->GetTexture(aiTextureType_DIFFUSE, texIndex, &texture_path);

      if (texFound == AI_SUCCESS) {
        std::string filename = trim_string(texture_path.C_Str());

        // Skip empty texture paths (common in materials without textures assigned)
        if (filename.empty()) {
          LOG_WARN("[MATERIAL] Skipping empty texture path for material '{}' for {}", material.name, model_path);
          continue;
        }

        // Fix path separators: convert Windows backslashes to forward slashes for cross-platform compatibility
        std::replace(filename.begin(), filename.end(), '\\', '/');

        LOG_TRACE("[MATERIAL] Trying to load texture: '{}'", filename);

        // Try 1: External file at model directory
        std::string fileloc = basepath + filename;
        if (std::filesystem::exists(fileloc)) {
          LOG_TRACE("[MATERIAL] Loading external texture: {}", fileloc);
          material.texture_id = texture_manager.load_texture_for_model(fileloc);
          if (material.texture_id != 0) {
            material.has_texture = true;
            break;
          }
        }

        // Try 2: Embedded textures
        for (unsigned int i = 0; i < scene->mNumTextures; i++) {
          const aiTexture* ai_tex = scene->mTextures[i];
          if (ai_tex) {
            // Embedded textures can be referenced by index (*0, *1, etc.) or by filename
            std::string embedded_name = "*" + std::to_string(i);
            if (filename == embedded_name ||
                (ai_tex->mFilename.length > 0 && filename == ai_tex->mFilename.C_Str())) {
              LOG_TRACE("[EMBEDDED] Loading embedded texture for '{}' at index {}", filename, i);
              material.texture_id = texture_manager.load_embedded_texture(ai_tex);
              if (material.texture_id != 0) {
                material.has_texture = true;
                break;
              }
            }
          }
        }

        // If we reach here, texture loading failed completely
        if (!material.has_texture) {
          LOG_ERROR("[MATERIAL] Failed to load texture '{}' - tried external path: '{}', {} embedded textures",
                    filename, basepath + filename, scene->mNumTextures);
        }

        // Even if texture loading failed, don't fail the entire model - use material colors as fallback
        if (material.has_texture) {
          break; // Use first successful texture
        }
      }
    }

    // Load material color properties
    aiColor3D diffuse_color(0.8f, 0.8f, 0.8f);
    aiColor3D ambient_color(0.2f, 0.2f, 0.2f);
    aiColor3D specular_color(0.0f, 0.0f, 0.0f);

    material.has_diffuse_color = (ai_material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse_color) == AI_SUCCESS);
    ai_material->Get(AI_MATKEY_COLOR_AMBIENT, ambient_color);
    ai_material->Get(AI_MATKEY_COLOR_SPECULAR, specular_color);
    ai_material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive_color);

    material.diffuse_color = glm::vec3(diffuse_color.r, diffuse_color.g, diffuse_color.b);
    material.ambient_color = glm::vec3(ambient_color.r, ambient_color.g, ambient_color.b);
    material.specular_color = glm::vec3(specular_color.r, specular_color.g, specular_color.b);
    material.emissive_color = glm::vec3(emissive_color.r, emissive_color.g, emissive_color.b);

    // Check if material has significant emissive properties
    material.has_emissive = (material.emissive_color.r > 0.01f ||
      material.emissive_color.g > 0.01f ||
      material.emissive_color.b > 0.01f);

    // Debug: Log all color properties
    LOG_TRACE("[MATERIAL]   Diffuse color: ({:.3f}, {:.3f}, {:.3f})",
      diffuse_color.r, diffuse_color.g, diffuse_color.b);
    LOG_TRACE("[MATERIAL]   Ambient color: ({:.3f}, {:.3f}, {:.3f})",
      ambient_color.r, ambient_color.g, ambient_color.b);
    LOG_TRACE("[MATERIAL]   Specular color: ({:.3f}, {:.3f}, {:.3f})",
      specular_color.r, specular_color.g, specular_color.b);

    // Check for additional properties that could cause glow effects
    float shininess = 0.0f;
    float opacity = 1.0f;
    float reflectivity = 0.0f;
    float refraction_index = 1.0f;

    if (ai_material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS) {
      material.shininess = shininess;
      LOG_TRACE("[MATERIAL]   Shininess: {:.3f}", shininess);
    }
    if (ai_material->Get(AI_MATKEY_EMISSIVE_INTENSITY, emissive_intensity) == AI_SUCCESS) {
      material.emissive_intensity = emissive_intensity;
      LOG_TRACE("[MATERIAL]   Emissive intensity: {:.3f}", emissive_intensity);
    }
    if (ai_material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
      LOG_TRACE("[MATERIAL]   Opacity: {:.3f}", opacity);
    }
    if (ai_material->Get(AI_MATKEY_REFLECTIVITY, reflectivity) == AI_SUCCESS) {
      LOG_TRACE("[MATERIAL]   Reflectivity: {:.3f}", reflectivity);
    }
    if (ai_material->Get(AI_MATKEY_REFRACTI, refraction_index) == AI_SUCCESS) {
      LOG_TRACE("[MATERIAL]   Refraction index: {:.3f}", refraction_index);
    }

    // Check if material color is black (0,0,0) and set a default if so
    if (material.diffuse_color.x == 0.0f && material.diffuse_color.y == 0.0f && material.diffuse_color.z == 0.0f) {
      material.diffuse_color = glm::vec3(0.8f, 0.8f, 0.8f);
    }

    // Create material texture if no texture was loaded (blends diffuse + emissive)
    if (!material.has_texture) {
      LOG_TRACE("[MATERIAL] No texture loaded for material '{}', creating material texture with diffuse=({:.3f}, {:.3f}, {:.3f}) + emissive=({:.3f}, {:.3f}, {:.3f})",
        material.name,
        material.diffuse_color.x, material.diffuse_color.y, material.diffuse_color.z,
        material.emissive_color.x, material.emissive_color.y, material.emissive_color.z);
      material.texture_id = texture_manager.create_material_texture(
        material.diffuse_color, material.emissive_color, material.emissive_intensity);
    }

    LOG_TRACE("[MATERIAL] Final material '{}': has_texture={}, texture_id={}",
      material.name, material.has_texture, material.texture_id);
    model.materials.push_back(material);
  }
}

// Forward declarations
void process_node(aiNode* node, const aiScene* scene, Model& model, glm::mat4 parent_transform);
void process_mesh(aiMesh* mesh, const aiScene* scene, Model& model, glm::mat4 transform);
unsigned int load_model_texture(const aiScene* scene, const std::string& model_path);
unsigned int load_embedded_texture(const aiTexture* ai_texture);
void load_model_skeleton(const aiScene* scene, Model& model);

// Helper function to convert aiMatrix4x4 to glm::mat4
glm::mat4 ai_to_glm_mat4(const aiMatrix4x4& from) {
  glm::mat4 to;
  to[0][0] = from.a1;
  to[1][0] = from.a2;
  to[2][0] = from.a3;
  to[3][0] = from.a4;
  to[0][1] = from.b1;
  to[1][1] = from.b2;
  to[2][1] = from.b3;
  to[3][1] = from.b4;
  to[0][2] = from.c1;
  to[1][2] = from.c2;
  to[2][2] = from.c3;
  to[3][2] = from.c4;
  to[0][3] = from.d1;
  to[1][3] = from.d2;
  to[2][3] = from.d3;
  to[3][3] = from.d4;
  return to;
}

// Process node recursively and apply transformations
// Traverse the Assimp scene graph, baking each node's transform into mesh vertices.
void process_node(aiNode* node, const aiScene* scene, Model& model, glm::mat4 parent_transform) {
  // Calculate this node's transformation
  glm::mat4 node_transform = ai_to_glm_mat4(node->mTransformation);
  glm::mat4 final_transform = parent_transform * node_transform;

  // Process all meshes in this node
  for (unsigned int i = 0; i < node->mNumMeshes; i++) {
    aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
    process_mesh(mesh, scene, model, final_transform);
  }

  // Process all child nodes
  for (unsigned int i = 0; i < node->mNumChildren; i++) {
    process_node(node->mChildren[i], scene, model, final_transform);
  }
}

// Process a single mesh with transformation applied
// Convert a single Assimp mesh into our interleaved vertex/index buffers.
void process_mesh(aiMesh* mesh, const aiScene* /*scene*/, Model& model, glm::mat4 transform) {

  // Create mesh info
  Mesh mesh_info;
  mesh_info.name = mesh->mName.C_Str();
  mesh_info.material_index = mesh->mMaterialIndex;
  mesh_info.vertex_offset = static_cast<unsigned int>(model.vertices.size() / VERTEX_FLOAT_STRIDE);
  mesh_info.index_offset = static_cast<unsigned int>(model.indices.size());

  const bool has_bones = mesh->HasBones();
  mesh_info.has_skin = has_bones;
  if (has_bones) {
    model.has_skinned_meshes = true;
  }

  // Create normal matrix for proper normal transformation
  glm::mat3 normal_matrix = glm::mat3(glm::transpose(glm::inverse(transform)));

  std::vector<std::array<int, MODEL_MAX_BONES_PER_VERTEX>> vertex_bone_ids;
  std::vector<std::array<float, MODEL_MAX_BONES_PER_VERTEX>> vertex_bone_weights;
  if (has_bones) {
    vertex_bone_ids.resize(mesh->mNumVertices);
    vertex_bone_weights.resize(mesh->mNumVertices);

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
      vertex_bone_ids[i].fill(0);
      vertex_bone_weights[i].fill(0.0f);
    }

    for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
      const aiBone* ai_bone = mesh->mBones[b];
      if (!ai_bone) {
        continue;
      }

      const std::string raw_name = ai_bone->mName.C_Str();
      int bone_index = -1;

      auto raw_match = model.bone_lookup_raw.find(raw_name);
      if (raw_match != model.bone_lookup_raw.end()) {
        bone_index = raw_match->second;
      }
      else {
        const std::string clean_name = normalize_node_name(raw_name);
        auto clean_match = model.bone_lookup.find(clean_name);
        if (clean_match != model.bone_lookup.end()) {
          bone_index = clean_match->second;
        }
      }

      if (bone_index < 0) {
        LOG_WARN("[SKINNING] Mesh '{}' references unknown bone '{}'", mesh_info.name, raw_name);
        continue;
      }

      for (unsigned int w = 0; w < ai_bone->mNumWeights; ++w) {
        const aiVertexWeight& weight = ai_bone->mWeights[w];
        if (weight.mVertexId >= mesh->mNumVertices) {
          continue;
        }

        auto& ids = vertex_bone_ids[weight.mVertexId];
        auto& weights = vertex_bone_weights[weight.mVertexId];

        int target_slot = -1;
        for (int slot = 0; slot < MODEL_MAX_BONES_PER_VERTEX; ++slot) {
          if (weights[slot] == 0.0f) {
            target_slot = slot;
            break;
          }
        }

        if (target_slot == -1) {
          float min_weight = weights[0];
          int min_slot = 0;
          for (int slot = 1; slot < MODEL_MAX_BONES_PER_VERTEX; ++slot) {
            if (weights[slot] < min_weight) {
              min_weight = weights[slot];
              min_slot = slot;
            }
          }

          if (weight.mWeight <= min_weight) {
            continue;
          }

          target_slot = min_slot;
        }

        ids[target_slot] = bone_index;
        weights[target_slot] = weight.mWeight;
      }
    }

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
      auto& weights = vertex_bone_weights[i];
      float weight_sum = weights[0] + weights[1] + weights[2] + weights[3];
      if (weight_sum > 0.0f) {
        float inv_sum = 1.0f / weight_sum;
        for (float& value : weights) {
          value *= inv_sum;
        }
      }
      else {
        weights[0] = 1.0f;
      }
    }
  }

  // Process vertices with transformation applied
  for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
    // Transform position
    glm::vec4 pos(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z, 1.0f);
    glm::vec4 world_pos = transform * pos;
    model.min_bounds.x = std::min(model.min_bounds.x, world_pos.x);
    model.min_bounds.y = std::min(model.min_bounds.y, world_pos.y);
    model.min_bounds.z = std::min(model.min_bounds.z, world_pos.z);
    model.max_bounds.x = std::max(model.max_bounds.x, world_pos.x);
    model.max_bounds.y = std::max(model.max_bounds.y, world_pos.y);
    model.max_bounds.z = std::max(model.max_bounds.z, world_pos.z);

    glm::vec4 transformed_pos = has_bones ? pos : world_pos;

    model.vertices.push_back(transformed_pos.x);
    model.vertices.push_back(transformed_pos.y);
    model.vertices.push_back(transformed_pos.z);

    // Transform normal
    if (mesh->HasNormals()) {
      glm::vec3 normal(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
      glm::vec3 transformed_normal = has_bones ? normal : normal_matrix * normal;

      model.vertices.push_back(transformed_normal.x);
      model.vertices.push_back(transformed_normal.y);
      model.vertices.push_back(transformed_normal.z);
    }
    else {
      model.vertices.push_back(0.0f);
      model.vertices.push_back(0.0f);
      model.vertices.push_back(1.0f);
    }

    // Texture coordinates (no transformation needed)
    if (mesh->mTextureCoords[0]) {
      model.vertices.push_back(mesh->mTextureCoords[0][i].x);
      model.vertices.push_back(mesh->mTextureCoords[0][i].y);
    }
    else {
      model.vertices.push_back(0.0f);
      model.vertices.push_back(0.0f);
    }

    if (has_bones) {
      const auto& ids = vertex_bone_ids[i];
      const auto& weights = vertex_bone_weights[i];
      model.vertices.push_back(static_cast<float>(ids[0]));
      model.vertices.push_back(static_cast<float>(ids[1]));
      model.vertices.push_back(static_cast<float>(ids[2]));
      model.vertices.push_back(static_cast<float>(ids[3]));

      model.vertices.push_back(weights[0]);
      model.vertices.push_back(weights[1]);
      model.vertices.push_back(weights[2]);
      model.vertices.push_back(weights[3]);
    }
    else {
      model.vertices.push_back(0.0f);
      model.vertices.push_back(0.0f);
      model.vertices.push_back(0.0f);
      model.vertices.push_back(0.0f);

      model.vertices.push_back(0.0f);
      model.vertices.push_back(0.0f);
      model.vertices.push_back(0.0f);
      model.vertices.push_back(0.0f);
    }
  }

  // Process indices with proper offset
  for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
    aiFace face = mesh->mFaces[i];
    for (unsigned int j = 0; j < face.mNumIndices; j++) {
      model.indices.push_back(face.mIndices[j] + mesh_info.vertex_offset);
    }
  }

  // Store mesh info
  mesh_info.vertex_count = mesh->mNumVertices;
  mesh_info.index_count = mesh->mNumFaces * 3; // Assuming triangulated faces
  model.meshes.push_back(mesh_info);
}

// Load skeleton data from scene
// Build the bone hierarchy, rest pose, and lookup tables from the Assimp scene data.
void load_model_skeleton(const aiScene* scene, Model& model) {
  model.bones.clear();
  model.skeleton_nodes.clear();
  model.skeleton_node_lookup.clear();
  model.has_skeleton = false;

  if (!scene || !scene->mRootNode) {
    return;
  }

  LOG_DEBUG("[SKELETON] Scene has {} meshes, {} animations", scene->mNumMeshes, scene->mNumAnimations);

  std::unordered_map<std::string, int> bone_name_to_index;
  std::unordered_map<std::string, int> bone_raw_name_to_index;

  auto register_bone = [&](const std::string& raw_name, const std::string& clean_name, const aiMatrix4x4* offset_matrix) {
    auto it = bone_name_to_index.find(clean_name);
    if (it == bone_name_to_index.end()) {
      Bone bone;
      bone.name = clean_name;
      bone.offset_matrix = offset_matrix ? ai_to_glm_mat4(*offset_matrix) : glm::mat4(1.0f);
      bone.local_transform = glm::mat4(1.0f);
      bone.global_transform = glm::mat4(1.0f);
      bone.parent_index = -1;
      bone.skeleton_node_index = -1;
      int index = static_cast<int>(model.bones.size());
      bone_name_to_index[clean_name] = index;
      bone_raw_name_to_index[raw_name] = index;
      model.bones.push_back(bone);
      LOG_DEBUG("[SKELETON] Registered bone '{}' (raw='{}', index {})", clean_name, raw_name, index);
    }
    else {
      bone_raw_name_to_index[raw_name] = it->second;
    }
  };

  for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
    const aiMesh* mesh = scene->mMeshes[m];
    for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
      const aiBone* ai_bone = mesh->mBones[b];
      std::string raw_name = ai_bone->mName.C_Str();
      std::string clean_name = normalize_node_name(raw_name);
      register_bone(raw_name, clean_name, &ai_bone->mOffsetMatrix);
    }
  }

  if (model.bones.empty() && scene->mNumAnimations > 0) {
  LOG_DEBUG("[SKELETON] No mesh bones; scanning animations for bone names");
    std::set<std::string> bone_names;
    for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
      const aiAnimation* anim = scene->mAnimations[a];
      for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
        const aiNodeAnim* channel = anim->mChannels[c];
        bone_names.insert(normalize_node_name(channel->mNodeName.C_Str()));
      }
    }

    for (const std::string& name : bone_names) {
      register_bone(name, name, nullptr);
    }
  }

  if (model.bones.empty()) {
    return;
  }

  model.bone_lookup = bone_name_to_index;
  model.bone_lookup_raw = bone_raw_name_to_index;

  std::function<void(aiNode*, int, int)> build_nodes = [&](aiNode* node, int parent_node_index, int parent_bone_index) {
    const std::string raw_name = node->mName.C_Str();
    const std::string clean_name = normalize_node_name(raw_name);

    SkeletonNode skeleton_node;
    skeleton_node.name_raw = raw_name;
    skeleton_node.name = clean_name;
    skeleton_node.rest_local_transform = ai_to_glm_mat4(node->mTransformation);
    skeleton_node.parent_index = parent_node_index;
    decompose_transform(skeleton_node.rest_local_transform, skeleton_node.rest_position, skeleton_node.rest_rotation, skeleton_node.rest_scale);

    int node_index = static_cast<int>(model.skeleton_nodes.size());
    model.skeleton_nodes.push_back(skeleton_node);
    model.skeleton_node_lookup[raw_name] = node_index;

    if (parent_node_index >= 0) {
      model.skeleton_nodes[parent_node_index].child_indices.push_back(node_index);
    }

    int current_bone_index = parent_bone_index;

    auto raw_match = bone_raw_name_to_index.find(raw_name);
    bool helper_tag = raw_name.find("_$AssimpFbx$_") != std::string::npos;

    if (raw_match != bone_raw_name_to_index.end() || (!helper_tag && bone_name_to_index.count(clean_name) != 0)) {
      current_bone_index = (raw_match != bone_raw_name_to_index.end()) ? raw_match->second : bone_name_to_index[clean_name];

      SkeletonNode& stored = model.skeleton_nodes[node_index];
      stored.bone_index = current_bone_index;
      stored.is_bone = !helper_tag;
      stored.is_helper = helper_tag;

      Bone& bone = model.bones[current_bone_index];
      if (stored.is_bone) {
        bone.parent_index = parent_bone_index;
        bone.skeleton_node_index = node_index;
        bone.local_transform = stored.rest_local_transform;
        bone.rest_position = stored.rest_position;
        bone.rest_rotation = stored.rest_rotation;
        bone.rest_scale = stored.rest_scale;
        if (parent_bone_index >= 0) {
          model.bones[parent_bone_index].child_indices.push_back(current_bone_index);
        }
      }
      else {
        stored.is_helper = true;
      }
    }
    else if (bone_name_to_index.count(clean_name) != 0) {
      SkeletonNode& stored = model.skeleton_nodes[node_index];
      stored.bone_index = bone_name_to_index[clean_name];
      stored.is_helper = true;
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
      build_nodes(node->mChildren[i], node_index, current_bone_index);
    }
  };

  build_nodes(scene->mRootNode, -1, -1);

  std::function<void(int, const glm::mat4&)> compute_rest_globals = [&](int node_index, const glm::mat4& parent_global) {
    SkeletonNode& node = model.skeleton_nodes[node_index];
    glm::mat4 global = parent_global * node.rest_local_transform;
    node.rest_global_transform = global;

    if (node.is_bone && node.bone_index >= 0) {
      Bone& bone = model.bones[node.bone_index];
      bone.global_transform = global;
    }

    for (int child_index : node.child_indices) {
      compute_rest_globals(child_index, global);
    }
  };

  for (size_t i = 0; i < model.skeleton_nodes.size(); ++i) {
    if (model.skeleton_nodes[i].parent_index == -1) {
      compute_rest_globals(static_cast<int>(i), glm::mat4(1.0f));
    }
  }

  model.animated_local_transforms.assign(model.bones.size(), glm::mat4(1.0f));
  model.animated_node_local_transforms.assign(model.skeleton_nodes.size(), glm::mat4(1.0f));
  model.animated_node_global_transforms.assign(model.skeleton_nodes.size(), glm::mat4(1.0f));
  model.has_skeleton = true;

  for (size_t i = 0; i < std::min<size_t>(model.bones.size(), 5); ++i) {
    const Bone& bone = model.bones[i];
    glm::vec3 pos = glm::vec3(bone.global_transform[3]);
    LOG_DEBUG("[SKELETON] Rest pose bone {} at ({:.3f}, {:.3f}, {:.3f})", bone.name, pos.x, pos.y, pos.z);
  }
}

// Load geometry, materials, skeleton, and animations for a model on demand.
bool load_model(const std::string& filepath, Model& model, TextureManager& texture_manager) {
  // Clean up previous model
  cleanup_model(model);

  // Initialize model state
  model.path = filepath;
  model.has_no_geometry = false;
  model.has_skinned_meshes = false;

  // Check if file exists
  if (!std::filesystem::exists(filepath)) {
    LOG_ERROR("Model file not found: {}", filepath);
    return false;
  }

  LOG_DEBUG("[3D] Loading model: {}", filepath);

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(
    filepath, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace
  );

  if (!scene || !scene->mRootNode) {
    LOG_ERROR("ASSIMP: Failed to load '{}' - {}", filepath, importer.GetErrorString());
    return false;
  }

  // Handle incomplete scenes (common with FBX files containing animations)
  if (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) {
    LOG_DEBUG("Scene marked as incomplete (possibly due to animations), but proceeding with mesh data");
  }

  model.min_bounds = aiVector3D(FLT_MAX, FLT_MAX, FLT_MAX);
  model.max_bounds = aiVector3D(-FLT_MAX, -FLT_MAX, -FLT_MAX);

  // Build skeleton first so bone indices are available during mesh processing
  load_model_skeleton(scene, model);

  // Process the scene hierarchy starting from root node
  glm::mat4 identity = glm::mat4(1.0f);
  process_node(scene->mRootNode, scene, model, identity);

  load_model_animations(scene, model);

  // Check if the model has any visible geometry
  bool has_geometry = !model.vertices.empty() && !model.indices.empty();
  if (!has_geometry) {
    model.has_no_geometry = true;

    // If there's no geometry but there is a skeleton, still allow loading for animation preview
    if (model.has_skeleton) {
      LOG_INFO("[3D] Animation-only file with {} bones (no geometry)", model.bones.size());

      // Calculate bounds from skeleton bone positions
      model.min_bounds = aiVector3D(FLT_MAX, FLT_MAX, FLT_MAX);
      model.max_bounds = aiVector3D(-FLT_MAX, -FLT_MAX, -FLT_MAX);

      for (const Bone& bone : model.bones) {
        glm::vec3 bone_pos = glm::vec3(bone.global_transform[3]);
        model.min_bounds.x = std::min(model.min_bounds.x, bone_pos.x);
        model.min_bounds.y = std::min(model.min_bounds.y, bone_pos.y);
        model.min_bounds.z = std::min(model.min_bounds.z, bone_pos.z);
        model.max_bounds.x = std::max(model.max_bounds.x, bone_pos.x);
        model.max_bounds.y = std::max(model.max_bounds.y, bone_pos.y);
        model.max_bounds.z = std::max(model.max_bounds.z, bone_pos.z);
      }

      model.loaded = true;
      return true;
    }

    return false;
  }

  // Calculate bounds
  if (!model.vertices.empty() && !model.has_skinned_meshes) {
    model.min_bounds = aiVector3D(FLT_MAX, FLT_MAX, FLT_MAX);
    model.max_bounds = aiVector3D(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (size_t i = 0; i < model.vertices.size(); i += VERTEX_FLOAT_STRIDE) {
      aiVector3D pos(model.vertices[i], model.vertices[i + 1], model.vertices[i + 2]);
      model.min_bounds.x = std::min(model.min_bounds.x, pos.x);
      model.min_bounds.y = std::min(model.min_bounds.y, pos.y);
      model.min_bounds.z = std::min(model.min_bounds.z, pos.z);
      model.max_bounds.x = std::max(model.max_bounds.x, pos.x);
      model.max_bounds.y = std::max(model.max_bounds.y, pos.y);
      model.max_bounds.z = std::max(model.max_bounds.z, pos.z);
    }
  }

  // Clear any existing OpenGL errors before we start
  while (glGetError() != GL_NO_ERROR) {}

  // Create OpenGL buffers
  glGenVertexArrays(1, &model.vao);
  glGenBuffers(1, &model.vbo);
  glGenBuffers(1, &model.ebo);

  if (model.vao == 0 || model.vbo == 0 || model.ebo == 0) {
    LOG_ERROR("Failed to generate OpenGL buffers!");
    return false;
  }

  glBindVertexArray(model.vao);

  glBindBuffer(GL_ARRAY_BUFFER, model.vbo);

  glBufferData(GL_ARRAY_BUFFER, model.vertices.size() * sizeof(float), model.vertices.data(), GL_STATIC_DRAW);

  // Check for OpenGL errors
  GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    LOG_ERROR("OpenGL error after vertex buffer creation: 0x{:X} ({})", error, error);
    cleanup_model(model);
    return false;
  }

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model.ebo);

  glBufferData(GL_ELEMENT_ARRAY_BUFFER, model.indices.size() * sizeof(unsigned int), model.indices.data(), GL_STATIC_DRAW);

  error = glGetError();
  if (error != GL_NO_ERROR) {
    LOG_ERROR("OpenGL error after index buffer creation: 0x{:X} ({})", error, error);
    cleanup_model(model);
    return false;
  }

  const GLsizei vertex_stride_bytes = VERTEX_FLOAT_STRIDE * sizeof(float);
  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertex_stride_bytes, (void*) (POSITION_OFFSET * sizeof(float)));
  glEnableVertexAttribArray(0);
  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, vertex_stride_bytes, (void*) (NORMAL_OFFSET * sizeof(float)));
  glEnableVertexAttribArray(1);
  // Texture coordinate attribute
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, vertex_stride_bytes, (void*) (TEXCOORD_OFFSET * sizeof(float)));
  glEnableVertexAttribArray(2);
  // Bone indices attribute (stored as floats to simplify layout)
  glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, vertex_stride_bytes, (void*) (BONE_ID_OFFSET * sizeof(float)));
  glEnableVertexAttribArray(3);
  // Bone weights attribute
  glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, vertex_stride_bytes, (void*) (BONE_WEIGHT_OFFSET * sizeof(float)));
  glEnableVertexAttribArray(4);

  glBindVertexArray(0);

  // Load all materials from the model
  load_model_materials(scene, filepath, model, texture_manager);

  LOG_DEBUG("[3D] Loaded {} materials", model.materials.size());

  model.loaded = true;
  return true;
}

// Draw the model meshes with lighting sized to the preview camera.
void render_model(const Model& model, TextureManager& texture_manager, const Camera3D& camera, bool draw_axes) {
  if (!model.loaded)
    return;

  if (shader_ == 0) {
    LOG_ERROR("[3D] render_model called without initialized shader program");
    return;
  }

  glUseProgram(shader_);

  // Set up matrices
  glm::mat4 model_matrix = glm::mat4(1.0f);

  // Center and position the model to ensure it's always visible
  aiVector3D center = (model.min_bounds + model.max_bounds) * 0.5f;
  aiVector3D size = model.max_bounds - model.min_bounds;
  float max_size = std::max({ size.x, size.y, size.z });

  // Just center the model at the origin
  model_matrix = glm::translate(model_matrix, glm::vec3(-center.x, -center.y, -center.z));

  // Calculate camera distance based on model size and zoom
  // We want the model to fit nicely in the view, so move camera back based on size
  float base_distance = max_size * 2.2f; // 2.0x the model size for good framing
  float camera_distance = base_distance / camera.zoom; // Apply zoom factor

  // Convert rotation angles to radians
  float rot_x_rad = glm::radians(camera.rotation_x);
  float rot_y_rad = glm::radians(camera.rotation_y);

  // Calculate camera position using spherical coordinates
  float camera_x = camera_distance * cos(rot_x_rad) * sin(rot_y_rad);
  float camera_y = camera_distance * sin(rot_x_rad);
  float camera_z = camera_distance * cos(rot_x_rad) * cos(rot_y_rad);

  glm::mat4 view_matrix = glm::lookAt(
    glm::vec3(camera_x, camera_y, camera_z), // Camera position - controlled by mouse
    glm::vec3(0.0f, 0.0f, 0.0f),             // Look at origin where model is centered
    glm::vec3(0.0f, 1.0f, 0.0f)              // Standard +Y-up view
  );

  // Set uniforms
  glUniformMatrix4fv(glGetUniformLocation(shader_, "model"), 1, GL_FALSE, glm::value_ptr(model_matrix));
  glUniformMatrix4fv(glGetUniformLocation(shader_, "view"), 1, GL_FALSE, glm::value_ptr(view_matrix));

  // Dynamic far clipping plane based on camera distance
  float far_plane = camera_distance * 2.0f; // 2x camera distance to ensure model is visible
  glm::mat4 projection_matrix = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, far_plane);
  glUniformMatrix4fv(
    glGetUniformLocation(shader_, "projection"), 1, GL_FALSE,
    glm::value_ptr(projection_matrix)
  );

  glm::vec3 camera_pos(camera_x, camera_y, camera_z);

  // Treat lighting as a headlamp attached to the camera
  const glm::vec3 light_direction = compute_preview_light_direction(camera_pos);
  glUniform3fv(glGetUniformLocation(shader_, "lightDir"), 1, &light_direction[0]);
  glUniform3f(glGetUniformLocation(shader_, "lightColor"), 1.0f, 1.0f, 1.0f);

  // Lighting intensity controls (boosted for brighter, more saturated appearance)
  glUniform1f(glGetUniformLocation(shader_, "ambientIntensity"), 0.5f);
  glUniform1f(glGetUniformLocation(shader_, "diffuseIntensity"), 0.5f);

  GLint enable_skinning_uniform = glGetUniformLocation(shader_, "enableSkinning");
  GLint bone_count_uniform = glGetUniformLocation(shader_, "boneCount");
  GLint bone_matrices_uniform = glGetUniformLocation(shader_, "boneMatrices");

  size_t bone_count = std::min(model.bones.size(), static_cast<size_t>(MAX_SHADER_BONES));
  static bool warned_bone_limit = false;
  if (model.has_skinned_meshes && model.bones.size() > MAX_SHADER_BONES && !warned_bone_limit) {
    LOG_WARN("[SKINNING] Model '{}' uses {} bones but shader supports {}. Extra bones will be ignored.",
      model.path, model.bones.size(), MAX_SHADER_BONES);
    warned_bone_limit = true;
  }
  static bool warned_missing_bones = false;
  if (model.has_skinned_meshes && bone_count == 0 && !warned_missing_bones) {
    LOG_WARN("[SKINNING] Model '{}' has skinned meshes but no bones were loaded; rendering may be incorrect.",
      model.path);
    warned_missing_bones = true;
  }

  static std::vector<glm::mat4> bone_matrices;
  if (bone_count_uniform >= 0) {
    glUniform1i(bone_count_uniform, static_cast<int>(bone_count));
  }

  if (bone_count > 0 && bone_matrices_uniform >= 0) {
    bone_matrices.resize(bone_count);
    for (size_t i = 0; i < bone_count; ++i) {
      const Bone& bone = model.bones[i];
      bone_matrices[i] = model_matrix * bone.global_transform * bone.offset_matrix;
    }
    glUniformMatrix4fv(bone_matrices_uniform, static_cast<GLsizei>(bone_count), GL_FALSE,
      glm::value_ptr(bone_matrices[0]));
  }
  else {
    bone_matrices.clear();
  }

  const bool has_renderable_geometry = (model.vao != 0) && !model.indices.empty();

  if (has_renderable_geometry) {
    glBindVertexArray(model.vao);

    if (model.meshes.empty()) {
      // Fallback: render as single mesh with first material if available
      if (!model.materials.empty()) {
        const Material& material = model.materials[0];
        if (enable_skinning_uniform >= 0) {
          glUniform1i(enable_skinning_uniform, (model.has_skinned_meshes && bone_count > 0) ? 1 : 0);
        }
        if (material.has_texture && material.texture_id != 0) {
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, material.texture_id);
          glUniform1i(glGetUniformLocation(shader_, "diffuseTexture"), 0);
          glUniform1i(glGetUniformLocation(shader_, "useTexture"), 1);
        }
        else {
          glUniform1i(glGetUniformLocation(shader_, "useTexture"), 0);
          glUniform3fv(glGetUniformLocation(shader_, "materialColor"), 1, &material.diffuse_color[0]);
        }
        // Always pass emissive color (will be zero if material has no emissive)
        glUniform3fv(glGetUniformLocation(shader_, "emissiveColor"), 1, &material.emissive_color[0]);
      }
      else {
        glUniform1i(glGetUniformLocation(shader_, "useTexture"), 0);
        glm::vec3 default_color(0.7f, 0.7f, 0.7f);
        glUniform3fv(glGetUniformLocation(shader_, "materialColor"), 1, &default_color[0]);
        // No emissive for default material
        glm::vec3 no_emissive(0.0f, 0.0f, 0.0f);
        glUniform3fv(glGetUniformLocation(shader_, "emissiveColor"), 1, &no_emissive[0]);
      }

      glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(model.indices.size()), GL_UNSIGNED_INT, 0);
    }
    else {
      // Render each mesh with its correct material
      for (const auto& mesh : model.meshes) {
        if (enable_skinning_uniform >= 0) {
          glUniform1i(enable_skinning_uniform, (mesh.has_skin && bone_count > 0) ? 1 : 0);
        }
        if (mesh.material_index < model.materials.size()) {
          const Material& material = model.materials[mesh.material_index];

          // Bind texture or set material color
          if (material.has_texture && material.texture_id != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, material.texture_id);
            glUniform1i(glGetUniformLocation(shader_, "diffuseTexture"), 0);
            glUniform1i(glGetUniformLocation(shader_, "useTexture"), 1);
          }
          else {
            glUniform1i(glGetUniformLocation(shader_, "useTexture"), 0);
            glUniform3fv(glGetUniformLocation(shader_, "materialColor"), 1, &material.diffuse_color[0]);
          }
          // Always pass emissive color for this material
          glUniform3fv(glGetUniformLocation(shader_, "emissiveColor"), 1, &material.emissive_color[0]);

          // Draw this specific mesh (indices are already properly offset)
          glDrawElements(
            GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, (void*) (mesh.index_offset * sizeof(unsigned int))
          );
        }
      }
    }

    glBindVertexArray(0);
  }
  else {
    glUniform1i(glGetUniformLocation(shader_, "useTexture"), 0);
    if (enable_skinning_uniform >= 0) {
      glUniform1i(enable_skinning_uniform, 0);
    }
  }

  if (enable_skinning_uniform >= 0) {
    glUniform1i(enable_skinning_uniform, 0);
  }

  // Ensure subsequent passes (debug axes, skeleton overlay) render with solid colors.
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glUniform1i(glGetUniformLocation(shader_, "useTexture"), 0);

  // Render debug axes at origin (scaled relative to model size)
  // Pass the same view and projection matrices to ensure consistency
  if (draw_axes && Config::PREVIEW_DRAW_DEBUG_AXES) {
    float axis_scale = max_size * 0.7f;
    render_debug_axes(axis_scale, view_matrix, projection_matrix, light_direction);
  }
}

// Helper function to create diamond-shaped bone geometry (two pyramids base-to-base)
// Wide base is placed closer to start (parent) to show bone directionality
void generate_bone_diamond(const glm::vec3& start, const glm::vec3& end, float width,
                           std::vector<float>& vertices, std::vector<unsigned int>& indices) {
  // Calculate bone direction and length
  glm::vec3 direction = end - start;
  float length = glm::length(direction);
  if (length < 0.0001f) return; // Skip zero-length bones

  glm::vec3 dir_normalized = glm::normalize(direction);

  // Place wide base 20% along the bone (closer to parent/start)
  glm::vec3 base_pos = start + dir_normalized * (length * 0.20f);

  // Create perpendicular vectors for the square cross-section
  glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
  if (std::abs(glm::dot(dir_normalized, up)) > 0.99f) {
    up = glm::vec3(1.0f, 0.0f, 0.0f); // Use different up if parallel
  }
  glm::vec3 right = glm::normalize(glm::cross(dir_normalized, up));
  glm::vec3 forward = glm::normalize(glm::cross(right, dir_normalized));

  // Create 6 vertices: start, end, and 4 corners at base_pos
  unsigned int base_idx = vertices.size() / 8;

  // Vertex 0: start point (apex of first pyramid)
  glm::vec3 start_normal = glm::normalize(start - base_pos);
  vertices.insert(vertices.end(), {start.x, start.y, start.z, start_normal.x, start_normal.y, start_normal.z, 0.0f, 0.0f});

  // Vertex 1: end point (apex of second pyramid)
  glm::vec3 end_normal = glm::normalize(end - base_pos);
  vertices.insert(vertices.end(), {end.x, end.y, end.z, end_normal.x, end_normal.y, end_normal.z, 0.0f, 0.0f});

  // Vertices 2-5: four corners of the diamond at base_pos
  glm::vec3 corners[4] = {
    base_pos + right * width + forward * width,   // Corner 0: +X +Z
    base_pos - right * width + forward * width,   // Corner 1: -X +Z
    base_pos - right * width - forward * width,   // Corner 2: -X -Z
    base_pos + right * width - forward * width    // Corner 3: +X -Z
  };

  for (int i = 0; i < 4; i++) {
    glm::vec3 corner_normal = glm::normalize(corners[i] - base_pos);
    vertices.insert(vertices.end(), {corners[i].x, corners[i].y, corners[i].z,
                                     corner_normal.x, corner_normal.y, corner_normal.z, 0.0f, 0.0f});
  }

  // Create triangles using the shared vertices
  // First pyramid: start (0) to each edge of the square base
  for (int i = 0; i < 4; i++) {
    indices.push_back(base_idx + 0);           // start
    indices.push_back(base_idx + 2 + i);       // current corner
    indices.push_back(base_idx + 2 + (i+1)%4); // next corner
  }

  // Second pyramid: end (1) to each edge of the square base (reversed winding)
  for (int i = 0; i < 4; i++) {
    indices.push_back(base_idx + 1);           // end
    indices.push_back(base_idx + 2 + (i+1)%4); // next corner
    indices.push_back(base_idx + 2 + i);       // current corner
  }
}

void render_skeleton(const Model& model, const Camera3D& camera, TextureManager& texture_manager) {
  if (!model.has_skeleton || model.bones.empty()) {
    return;
  }

  // Use dedicated skeleton shader with directional lighting
  glUseProgram(shader_);

  // Ensure shader samples constant color instead of any residual texture state.
  glUniform1i(glGetUniformLocation(shader_, "useTexture"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  GLint enable_skinning_uniform = glGetUniformLocation(shader_, "enableSkinning");
  if (enable_skinning_uniform >= 0) {
    glUniform1i(enable_skinning_uniform, 0);
  }

  // Set up matrices (same as render_model)
  glm::mat4 model_matrix = glm::mat4(1.0f);

  // Center the skeleton at origin (same centering as model)
  aiVector3D center = (model.min_bounds + model.max_bounds) * 0.5f;
  model_matrix = glm::translate(model_matrix, glm::vec3(-center.x, -center.y, -center.z));

  // Calculate camera matrices (same as render_model)
  aiVector3D size = model.max_bounds - model.min_bounds;
  float max_size = std::max({ size.x, size.y, size.z });
  float base_distance = max_size * 2.2f;
  float camera_distance = base_distance / camera.zoom;

  float rot_x_rad = glm::radians(camera.rotation_x);
  float rot_y_rad = glm::radians(camera.rotation_y);

  float camera_x = camera_distance * cos(rot_x_rad) * sin(rot_y_rad);
  float camera_y = camera_distance * sin(rot_x_rad);
  float camera_z = camera_distance * cos(rot_x_rad) * cos(rot_y_rad);

  glm::mat4 view_matrix = glm::lookAt(
    glm::vec3(camera_x, camera_y, camera_z),
    glm::vec3(0.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f)
  );

  float far_plane = camera_distance * 2.0f;
  glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, far_plane);

  // Set uniforms for skeleton shader
  glUniformMatrix4fv(glGetUniformLocation(shader_, "model"), 1, GL_FALSE, glm::value_ptr(model_matrix));
  glUniformMatrix4fv(glGetUniformLocation(shader_, "view"), 1, GL_FALSE, glm::value_ptr(view_matrix));
  glUniformMatrix4fv(glGetUniformLocation(shader_, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

  glm::vec3 camera_pos(camera_x, camera_y, camera_z);

  // Directional lighting (same as models)
  const glm::vec3 light_direction = compute_preview_light_direction(camera_pos);
  glUniform3fv(glGetUniformLocation(shader_, "lightDir"), 1, &light_direction[0]);
  glUniform3f(glGetUniformLocation(shader_, "lightColor"), 1.0f, 1.0f, 1.0f);

  // Lighting intensity controls (0.3 ambient, 0.56 diffuse)
  glUniform1f(glGetUniformLocation(shader_, "ambientIntensity"), 0.3f);
  glUniform1f(glGetUniformLocation(shader_, "diffuseIntensity"), 0.56f);

  // Build vertex data for diamond-shaped bones
  std::vector<float> bone_vertices;
  std::vector<unsigned int> bone_indices;

  for (const Bone& bone : model.bones) {
    // Skip root bones (no parent to draw to)
    if (bone.parent_index < 0 || bone.parent_index >= static_cast<int>(model.bones.size())) {
      continue;
    }

    const Bone& parent_bone = model.bones[bone.parent_index];

    // Skip control/helper bones based on config filters
    // These are technical bones not part of the visual skeleton
    const bool hide_root_child = Config::SKELETON_HIDE_ROOT_CHILDREN && parent_bone.name == "Root";
    if ((Config::SKELETON_HIDE_CTRL_BONES && bone.name.find("Ctrl") != std::string::npos) ||
        (Config::SKELETON_HIDE_IK_BONES && bone.name.find("IK") != std::string::npos) ||
        (Config::SKELETON_HIDE_ROLL_BONES && bone.name.find("Roll") != std::string::npos) ||
        hide_root_child) {
      LOG_TRACE("[SKELETON] Skipping bone '{}' due to filters (parent '{}')", bone.name, parent_bone.name);
      continue;
    }

    // Extract positions from global transforms
    glm::vec3 bone_pos = glm::vec3(bone.global_transform[3]);
    glm::vec3 parent_pos = glm::vec3(parent_bone.global_transform[3]);

    // Calculate bone width relative to this bone's length (5% of bone length)
    float bone_length = glm::length(bone_pos - parent_pos);
    float bone_width = bone_length * 0.07f;

    // Generate diamond geometry for this bone
    generate_bone_diamond(parent_pos, bone_pos, bone_width, bone_vertices, bone_indices);
  }

  if (bone_vertices.empty()) {
    return;
  }

  // Create temporary VAO/VBO/EBO for bone geometry
  unsigned int bone_vao, bone_vbo, bone_ebo;
  glGenVertexArrays(1, &bone_vao);
  glGenBuffers(1, &bone_vbo);
  glGenBuffers(1, &bone_ebo);

  glBindVertexArray(bone_vao);

  glBindBuffer(GL_ARRAY_BUFFER, bone_vbo);
  glBufferData(GL_ARRAY_BUFFER, bone_vertices.size() * sizeof(float), bone_vertices.data(), GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bone_ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, bone_indices.size() * sizeof(unsigned int), bone_indices.data(), GL_DYNAMIC_DRAW);

  // Set up vertex attributes (same layout as model vertices)
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  // Set skeleton color (brighter to compensate for dim lighting, creating uniform matte grey)
  glm::vec3 skeleton_color(Theme::SKELETON_BONE.x, Theme::SKELETON_BONE.y, Theme::SKELETON_BONE.z);
  glUniform3fv(glGetUniformLocation(shader_, "materialColor"), 1, &skeleton_color[0]);
  glm::vec3 no_emissive(0.0f, 0.0f, 0.0f);
  glUniform3fv(glGetUniformLocation(shader_, "emissiveColor"), 1, &no_emissive[0]);

  // Draw bone geometry
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(bone_indices.size()), GL_UNSIGNED_INT, 0);

  // Cleanup
  glBindVertexArray(0);
  glDeleteBuffers(1, &bone_vbo);
  glDeleteBuffers(1, &bone_ebo);
  glDeleteVertexArrays(1, &bone_vao);
}

// Release GPU buffers and clear CPU-side state for a model.
void cleanup_model(Model& model) {
  if (model.loaded) {
    glDeleteVertexArrays(1, &model.vao);
    glDeleteBuffers(1, &model.vbo);
    glDeleteBuffers(1, &model.ebo);

    model.vao = 0;
    model.vbo = 0;
    model.ebo = 0;

    // Clean up all material textures
    for (auto& material : model.materials) {
      if (material.texture_id != 0) {
        glDeleteTextures(1, &material.texture_id);
        material.texture_id = 0;
      }
    }

    model.vertices.clear();
    model.indices.clear();
    model.materials.clear();
    model.meshes.clear();
    model.bones.clear();
    model.bone_lookup.clear();
    model.bone_lookup_raw.clear();
    model.skeleton_nodes.clear();
    model.skeleton_node_lookup.clear();
    model.animations.clear();
    model.animated_local_transforms.clear();
    model.animated_node_local_transforms.clear();
    model.animated_node_global_transforms.clear();
    model.animation_playing = false;
    model.animation_time = 0.0;
    model.active_animation = 0;
    model.has_skeleton = false;
    model.has_skinned_meshes = false;
    model.has_no_geometry = false;
    model.loaded = false;
  }
}

// Replace the active preview model, releasing previous GL resources.
void set_current_model(Model& current_model, const Model& model) {
  // Clean up previous model
  cleanup_model(current_model);

  // Copy the new model
  current_model = model;
}

// Convenience getter to mirror legacy API expectations.
const Model& get_current_model(const Model& current_model) {
  return current_model;
}

// Draw origin axes so users have spatial reference inside the preview cube.
void render_debug_axes(float scale, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& light_direction) {
  static unsigned int axes_vao = 0;
  static unsigned int axes_vbo = 0;
  static bool initialized = false;

  if (!initialized) {
    // Create geometry for 3 axes with arrow heads
    // Each axis: line + cone/pyramid arrow head
    std::vector<float> vertices;

    // Arrow head proportions relative to axis length
    const float arrow_length = 0.15f;
    const float arrow_width = 0.05f;

    // X-axis (red) - pointing right
    // Line from origin to end
    vertices.insert(vertices.end(), {
      0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f,  // Origin
      1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f   // End
    });

    // Arrow head as a pyramid (4 triangular faces pointing in +X direction)
    float ax = 1.0f - arrow_length;  // Arrow base position
    float tip = 1.0f;                 // Arrow tip position

    // Triangle 1 (top)
    vertices.insert(vertices.end(), {
      ax, arrow_width, 0.0f,         0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      tip, 0.0f, 0.0f,               0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      ax, 0.0f, arrow_width,         0.0f, 1.0f, 0.0f,  0.0f, 0.0f
    });

    // Triangle 2 (right)
    vertices.insert(vertices.end(), {
      ax, 0.0f, arrow_width,         0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      tip, 0.0f, 0.0f,               0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      ax, -arrow_width, 0.0f,        0.0f, 1.0f, 0.0f,  0.0f, 0.0f
    });

    // Triangle 3 (bottom)
    vertices.insert(vertices.end(), {
      ax, -arrow_width, 0.0f,        0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      tip, 0.0f, 0.0f,               0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      ax, 0.0f, -arrow_width,        0.0f, 1.0f, 0.0f,  0.0f, 0.0f
    });

    // Triangle 4 (left)
    vertices.insert(vertices.end(), {
      ax, 0.0f, -arrow_width,        0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      tip, 0.0f, 0.0f,               0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      ax, arrow_width, 0.0f,         0.0f, 1.0f, 0.0f,  0.0f, 0.0f
    });

    // Y-axis (green) - pointing up
    vertices.insert(vertices.end(), {
      0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f
    });

    float ay = 1.0f - arrow_length;
    vertices.insert(vertices.end(), {
      arrow_width, ay, 0.0f,         0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, tip, 0.0f,               0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, ay, arrow_width,         0.0f, 1.0f, 0.0f,  0.0f, 0.0f,

      0.0f, ay, arrow_width,         0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, tip, 0.0f,               0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      -arrow_width, ay, 0.0f,        0.0f, 1.0f, 0.0f,  0.0f, 0.0f,

      -arrow_width, ay, 0.0f,        0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, tip, 0.0f,               0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, ay, -arrow_width,        0.0f, 1.0f, 0.0f,  0.0f, 0.0f,

      0.0f, ay, -arrow_width,        0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, tip, 0.0f,               0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      arrow_width, ay, 0.0f,         0.0f, 1.0f, 0.0f,  0.0f, 0.0f
    });

    // Z-axis (blue) - pointing forward
    vertices.insert(vertices.end(), {
      0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f
    });

    float az = 1.0f - arrow_length;
    vertices.insert(vertices.end(), {
      arrow_width, 0.0f, az,         0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, 0.0f, tip,               0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, arrow_width, az,         0.0f, 1.0f, 0.0f,  0.0f, 0.0f,

      0.0f, arrow_width, az,         0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, 0.0f, tip,               0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      -arrow_width, 0.0f, az,        0.0f, 1.0f, 0.0f,  0.0f, 0.0f,

      -arrow_width, 0.0f, az,        0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, 0.0f, tip,               0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, -arrow_width, az,        0.0f, 1.0f, 0.0f,  0.0f, 0.0f,

      0.0f, -arrow_width, az,        0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      0.0f, 0.0f, tip,               0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
      arrow_width, 0.0f, az,         0.0f, 1.0f, 0.0f,  0.0f, 0.0f
    });

    // Create OpenGL buffers
    glGenVertexArrays(1, &axes_vao);
    glGenBuffers(1, &axes_vbo);

    glBindVertexArray(axes_vao);
    glBindBuffer(GL_ARRAY_BUFFER, axes_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    // Vertex attributes (position, normal, texcoord)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    initialized = true;
  }

  // Render the axes using skeleton shader (simple directional lighting)
  glUseProgram(shader_);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glUniform1i(glGetUniformLocation(shader_, "useTexture"), 0);
  GLint enable_skinning_uniform = glGetUniformLocation(shader_, "enableSkinning");
  if (enable_skinning_uniform >= 0) {
    glUniform1i(enable_skinning_uniform, 0);
  }

  // Setup matrices - use identity model matrix scaled to axis size
  glm::mat4 model = glm::mat4(1.0f);
  model = glm::scale(model, glm::vec3(scale));

  glUniformMatrix4fv(glGetUniformLocation(shader_, "model"), 1, GL_FALSE, glm::value_ptr(model));
  glUniformMatrix4fv(glGetUniformLocation(shader_, "view"), 1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(shader_, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

  glUniform3fv(glGetUniformLocation(shader_, "lightDir"), 1, &light_direction[0]);
  glUniform3f(glGetUniformLocation(shader_, "lightColor"), 1.0f, 1.0f, 1.0f);
  glUniform3f(glGetUniformLocation(shader_, "emissiveColor"), 0.0f, 0.0f, 0.0f);

  // Lighting intensity controls (high ambient to mostly show axis colors)
  glUniform1f(glGetUniformLocation(shader_, "ambientIntensity"), 0.8f);
  glUniform1f(glGetUniformLocation(shader_, "diffuseIntensity"), 0.3f);

  glBindVertexArray(axes_vao);

  // Draw X-axis (red) - 2 vertices for line + 12 vertices for arrow (4 triangles)
  glUniform3f(glGetUniformLocation(shader_, "materialColor"), 1.0f, 0.0f, 0.0f);
  glDrawArrays(GL_LINES, 0, 2);
  glDrawArrays(GL_TRIANGLES, 2, 12);

  // Draw Y-axis (green) - starts at vertex 14
  glUniform3f(glGetUniformLocation(shader_, "materialColor"), 0.0f, 1.0f, 0.0f);
  glDrawArrays(GL_LINES, 14, 2);
  glDrawArrays(GL_TRIANGLES, 16, 12);

  // Draw Z-axis (blue) - starts at vertex 28
  glUniform3f(glGetUniformLocation(shader_, "materialColor"), 0.0f, 0.0f, 1.0f);
  glDrawArrays(GL_LINES, 28, 2);
  glDrawArrays(GL_TRIANGLES, 30, 12);

  glBindVertexArray(0);
}

// Render the off-screen preview framebuffer, advancing animation playback if needed.
void render_3d_preview(int width, int height, Model& model, TextureManager& texture_manager, const Camera3D& camera, float delta_time) {
  if (!texture_manager.is_preview_initialized()) {
    return;
  }

  // Update framebuffer size if needed
  static int last_fb_width = 0, last_fb_height = 0;
  if (width != last_fb_width || height != last_fb_height) {
    // Recreate framebuffer with new size
    glBindTexture(GL_TEXTURE_2D, texture_manager.get_preview_texture());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, texture_manager.get_preview_depth_texture());
    glTexImage2D(
      GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr
    );
    last_fb_width = width;
    last_fb_height = height;
  }

  // Render to framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, texture_manager.get_preview_framebuffer());
  glViewport(0, 0, width, height);
  glClearColor(
    Theme::BACKGROUND_LIGHT_BLUE_1.x, Theme::BACKGROUND_LIGHT_BLUE_1.y, Theme::BACKGROUND_LIGHT_BLUE_1.z,
    Theme::BACKGROUND_LIGHT_BLUE_1.w
  );
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (model.loaded) {
    if (Config::PREVIEW_PLAY_ANIMATIONS && model.animation_playing && !model.animations.empty() && !model.bones.empty()) {
      advance_model_animation(model, delta_time);
    }

    render_model(model, texture_manager, camera, true);

    // Render skeleton overlay if present
    if (model.has_skeleton) {
      render_skeleton(model, camera, texture_manager);
    }
  }

  // Unbind framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Configure global GL state expected by all preview renders (depth, blending, etc.).
void setup_3d_rendering_state() {
  // Enable depth testing for proper 3D rendering
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);

  // Enable blending for transparency support
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Note: Face culling is NOT enabled to handle models with inconsistent winding order
  // Some models have inverted normals or mixed winding, so we render all faces
}

// Helper function to compile a shader from source code
// Compile a single shader object and surface compile errors to the log.
static unsigned int compile_shader(unsigned int type, const std::string& source, const std::string& shader_name) {
  unsigned int shader = glCreateShader(type);
  const char* src = source.c_str();
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);

  int success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char info_log[512];
    glGetShaderInfoLog(shader, 512, nullptr, info_log);
    LOG_ERROR("Shader compilation failed ({}): {}", shader_name, info_log);
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

// Load shader source code from file and compile/link into OpenGL shader program
// Read, compile, and link the unified shader program used by all preview passes.
unsigned int load_shader_program(const std::string& vertex_path, const std::string& fragment_path) {
  // Read vertex shader source
  std::ifstream v_file(vertex_path);
  if (!v_file.is_open()) {
    LOG_ERROR("Failed to open vertex shader file: {}", vertex_path);
    return 0;
  }
  std::string vertex_source((std::istreambuf_iterator<char>(v_file)), std::istreambuf_iterator<char>());
  v_file.close();

  // Read fragment shader source
  std::ifstream f_file(fragment_path);
  if (!f_file.is_open()) {
    LOG_ERROR("Failed to open fragment shader file: {}", fragment_path);
    return 0;
  }
  std::string fragment_source((std::istreambuf_iterator<char>(f_file)), std::istreambuf_iterator<char>());
  f_file.close();

  // Compile shaders
  unsigned int vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source, vertex_path);
  if (vertex_shader == 0) {
    return 0;
  }

  unsigned int fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source, fragment_path);
  if (fragment_shader == 0) {
    glDeleteShader(vertex_shader);
    return 0;
  }

  // Link shader program
  unsigned int program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);

  int success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    char info_log[512];
    glGetProgramInfoLog(program, 512, nullptr, info_log);
    LOG_ERROR("Shader program linking failed: {}", info_log);
    glDeleteProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return 0;
  }

  // Clean up individual shaders (program has them linked now)
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  LOG_DEBUG("Loaded shader program: {} + {}", vertex_path, fragment_path);
  return program;
}

// Initialize 3D shaders by loading from external shader files
// Load the on-disk vertex/fragment shaders once per GL context.
bool initialize_3d_shaders() {
  LOG_DEBUG("Initializing unified 3D shader from external files");

  // Load unified shader (for all 3D rendering with parameter-based control)
  shader_ = load_shader_program("shaders/unified.vert", "shaders/unified.frag");
  if (shader_ == 0) {
    LOG_ERROR("Failed to load unified shader");
    return false;
  }

  LOG_INFO("Successfully initialized unified 3D shader");
  return true;
}

// Cleanup 3D shader programs
// Destroy shader program resources before tearing down the GL context.
void cleanup_3d_shaders() {
  if (shader_ != 0) {
    glDeleteProgram(shader_);
    shader_ = 0;
  }
}

std::vector<std::string> extract_model_texture_paths(const std::string& model_path) {
  std::vector<std::string> texture_paths;

  // Use Assimp to load the model and extract texture paths
  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(
    model_path,
    aiProcess_Triangulate | aiProcess_FlipUVs
  );

  if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
    LOG_WARN("[TEXTURE_EXTRACT] Failed to load model for texture extraction: {}", model_path);
    return texture_paths;
  }

  // Get model directory for resolving relative paths
  std::filesystem::path model_file_path = std::filesystem::u8path(model_path);
  std::string basepath = model_file_path.parent_path().string();
  if (!basepath.empty() && basepath.back() != std::filesystem::path::preferred_separator) {
    basepath += std::filesystem::path::preferred_separator;
  }

  // Set of unique texture paths (to avoid duplicates)
  std::set<std::string> unique_textures;

  // All texture types to check
  const std::vector<aiTextureType> texture_types = {
    aiTextureType_DIFFUSE,
    aiTextureType_SPECULAR,
    aiTextureType_AMBIENT,
    aiTextureType_EMISSIVE,
    aiTextureType_HEIGHT,
    aiTextureType_NORMALS,
    aiTextureType_SHININESS,
    aiTextureType_OPACITY,
    aiTextureType_DISPLACEMENT,
    aiTextureType_LIGHTMAP,
    aiTextureType_REFLECTION,
    aiTextureType_BASE_COLOR,
    aiTextureType_NORMAL_CAMERA,
    aiTextureType_EMISSION_COLOR,
    aiTextureType_METALNESS,
    aiTextureType_DIFFUSE_ROUGHNESS,
    aiTextureType_AMBIENT_OCCLUSION
  };

  // Iterate through all materials
  for (unsigned int m = 0; m < scene->mNumMaterials; m++) {
    const aiMaterial* material = scene->mMaterials[m];

    // Check each texture type
    for (aiTextureType tex_type : texture_types) {
      unsigned int tex_count = material->GetTextureCount(tex_type);

      for (unsigned int t = 0; t < tex_count; t++) {
        aiString texture_path;
        if (material->GetTexture(tex_type, t, &texture_path) == AI_SUCCESS) {
          std::string tex_path_str = trim_string(texture_path.C_Str());

          // Skip empty paths and embedded textures (referenced as *0, *1, etc.)
          if (tex_path_str.empty() || tex_path_str[0] == '*') {
            continue;
          }

          // Normalize path separators to forward slashes
          std::replace(tex_path_str.begin(), tex_path_str.end(), '\\', '/');

          // Check if texture file exists (relative to model directory)
          std::string full_path = basepath + tex_path_str;
          if (std::filesystem::exists(full_path)) {
            unique_textures.insert(tex_path_str);
          }
        }
      }
    }
  }

  // Convert set to vector
  texture_paths.assign(unique_textures.begin(), unique_textures.end());

  LOG_DEBUG("[TEXTURE_EXTRACT] Found {} texture reference(s) in model: {}",
            texture_paths.size(), model_path);

  return texture_paths;
}
