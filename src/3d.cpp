#include "3d.h"
#include "logger.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <set>
#include <map>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "stb_image.h"
#include "texture_manager.h"
#include "theme.h"
#include "utils.h"

// 3D Preview global variables are now managed by TextureManager

// 3D Preview no longer needs a global current model
// Model state is now managed by the caller

// Vertex shader source (updated for 3D models with texture support)
#ifdef __APPLE__
const char* vertex_shader_source = R"(
#version 410 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

void main()
{
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";
#else
const char* vertex_shader_source = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

void main()
{
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";
#endif

// Fragment shader source (updated for 3D models with multi-material support)
#ifdef __APPLE__
const char* fragment_shader_source = R"(
#version 410 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform sampler2D diffuseTexture;
uniform bool useTexture;
uniform vec3 materialColor;
uniform vec3 emissiveColor;

out vec4 FragColor;

void main()
{
    // Sample texture color or use material color
    vec3 objectColor = useTexture ? texture(diffuseTexture, TexCoord).rgb : materialColor;

    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);

    // Moderate ambient lighting to allow for some shadows
    float ambientStrength = 0.25;
    vec3 ambient = ambientStrength * lightColor;

    // Main key light (from camera direction)
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor * 0.7; // Slightly stronger main light

    // Add subtle fill light from opposite direction to soften shadows
    vec3 fillLightDir = normalize(-lightPos); // Opposite direction
    float fillDiff = max(dot(norm, fillLightDir), 0.0);
    vec3 fillLight = fillDiff * lightColor * 0.15; // Gentler fill light

    // Softer specular highlights
    float specularStrength = 0.2; // Reduced from 0.5
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 64); // Higher for softer highlights
    vec3 specular = specularStrength * spec * lightColor;

    // Add subtle rim lighting for better shape definition
    float rimStrength = 0.3;
    float rimFactor = 1.0 - max(dot(viewDir, norm), 0.0);
    vec3 rimLight = rimStrength * pow(rimFactor, 3.0) * lightColor;

    vec3 result = (ambient + diffuse + fillLight + specular + rimLight) * objectColor + emissiveColor;
    FragColor = vec4(result, 1.0);
}
)";
#else
const char* fragment_shader_source = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform sampler2D diffuseTexture;
uniform bool useTexture;
uniform vec3 materialColor;
uniform vec3 emissiveColor;

out vec4 FragColor;

void main()
{
    // Sample texture color or use material color
    vec3 objectColor = useTexture ? texture(diffuseTexture, TexCoord).rgb : materialColor;

    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);

    // Moderate ambient lighting to allow for some shadows
    float ambientStrength = 0.25;
    vec3 ambient = ambientStrength * lightColor;

    // Main key light (from camera direction)
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor * 0.7; // Slightly stronger main light

    // Add subtle fill light from opposite direction to soften shadows
    vec3 fillLightDir = normalize(-lightPos); // Opposite direction
    float fillDiff = max(dot(norm, fillLightDir), 0.0);
    vec3 fillLight = fillDiff * lightColor * 0.15; // Gentler fill light

    // Softer specular highlights
    float specularStrength = 0.2; // Reduced from 0.5
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 64); // Higher for softer highlights
    vec3 specular = specularStrength * spec * lightColor;

    // Add subtle rim lighting for better shape definition
    float rimStrength = 0.3;
    float rimFactor = 1.0 - max(dot(viewDir, norm), 0.0);
    vec3 rimLight = rimStrength * pow(rimFactor, 3.0) * lightColor;

    vec3 result = (ambient + diffuse + fillLight + specular + rimLight) * objectColor + emissiveColor;
    FragColor = vec4(result, 1.0);
}
)";
#endif

// Helper function to get base path
std::string getBasePath(const std::string& path) {
  size_t pos = path.find_last_of("\\/");
  return (std::string::npos == pos) ? "" : path.substr(0, pos + 1);
}


// Load all materials from model
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
void process_mesh(aiMesh* mesh, const aiScene* /*scene*/, Model& model, glm::mat4 transform) {

  // Create mesh info
  Mesh mesh_info;
  mesh_info.name = mesh->mName.C_Str();
  mesh_info.material_index = mesh->mMaterialIndex;
  mesh_info.vertex_offset = static_cast<unsigned int>(model.vertices.size() / 8); // 8 floats per vertex
  mesh_info.index_offset = static_cast<unsigned int>(model.indices.size());

  // Create normal matrix for proper normal transformation
  glm::mat3 normal_matrix = glm::mat3(glm::transpose(glm::inverse(transform)));

  // Process vertices with transformation applied
  for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
    // Transform position
    glm::vec4 pos(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z, 1.0f);
    glm::vec4 transformed_pos = transform * pos;

    model.vertices.push_back(transformed_pos.x);
    model.vertices.push_back(transformed_pos.y);
    model.vertices.push_back(transformed_pos.z);

    // Transform normal
    if (mesh->HasNormals()) {
      glm::vec3 normal(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
      glm::vec3 transformed_normal = normal_matrix * normal;

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
void load_model_skeleton(const aiScene* scene, Model& model) {
  model.bones.clear();
  model.has_skeleton = false;

  if (!scene || !scene->mRootNode) {
    return;
  }

  LOG_DEBUG("[SKELETON] Scene has {} meshes, {} animations", scene->mNumMeshes, scene->mNumAnimations);

  // Map bone names to indices for quick lookup
  std::map<std::string, int> bone_name_to_index;

  // First pass: collect all bones from all meshes
  for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
    const aiMesh* mesh = scene->mMeshes[m];
    LOG_DEBUG("[SKELETON] Mesh {} has {} bones", m, mesh->mNumBones);

    for (unsigned int b = 0; b < mesh->mNumBones; b++) {
      const aiBone* ai_bone = mesh->mBones[b];
      std::string bone_name = ai_bone->mName.C_Str();

      // Add bone if not already in our list
      if (bone_name_to_index.find(bone_name) == bone_name_to_index.end()) {
        Bone bone;
        bone.name = bone_name;
        bone.offset_matrix = ai_to_glm_mat4(ai_bone->mOffsetMatrix);
        bone.parent_index = -1; // Will be set in second pass

        bone_name_to_index[bone_name] = static_cast<int>(model.bones.size());
        model.bones.push_back(bone);
      }
    }
  }

  // If no bones found in meshes, try extracting from animation channels
  if (model.bones.empty() && scene->mNumAnimations > 0) {
    LOG_INFO("[SKELETON] No bones in meshes, trying to extract from {} animations", scene->mNumAnimations);

    // Collect unique bone names from all animation channels
    std::set<std::string> bone_names;
    for (unsigned int a = 0; a < scene->mNumAnimations; a++) {
      const aiAnimation* anim = scene->mAnimations[a];
      for (unsigned int c = 0; c < anim->mNumChannels; c++) {
        const aiNodeAnim* channel = anim->mChannels[c];
        bone_names.insert(channel->mNodeName.C_Str());
      }
    }

    LOG_INFO("[SKELETON] Found {} unique bone names in animations", bone_names.size());

    // Create bones from animation channels
    for (const std::string& bone_name : bone_names) {
      Bone bone;
      bone.name = bone_name;
      bone.offset_matrix = glm::mat4(1.0f); // Identity for animation-only bones
      bone.parent_index = -1; // Will be set in hierarchy build

      bone_name_to_index[bone_name] = static_cast<int>(model.bones.size());
      model.bones.push_back(bone);
    }
  }

  // If still no bones found, return early
  if (model.bones.empty()) {
    return;
  }

  LOG_INFO("[SKELETON] Found {} bones", model.bones.size());

  // Second pass: build hierarchy by traversing scene nodes
  std::function<void(aiNode*, int)> traverse_node = [&](aiNode* node, int parent_index) {
    std::string node_name = node->mName.C_Str();

    // Check if this node corresponds to a bone
    auto it = bone_name_to_index.find(node_name);
    int current_bone_index = -1;

    if (it != bone_name_to_index.end()) {
      current_bone_index = it->second;
      model.bones[current_bone_index].parent_index = parent_index;
      model.bones[current_bone_index].local_transform = ai_to_glm_mat4(node->mTransformation);

      // Add to parent's children list
      if (parent_index >= 0) {
        model.bones[parent_index].child_indices.push_back(current_bone_index);
      }

      LOG_TRACE("[SKELETON] Bone '{}' (index {}) has parent index {}",
                node_name, current_bone_index, parent_index);
    }

    // Recursively process children
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
      traverse_node(node->mChildren[i], current_bone_index >= 0 ? current_bone_index : parent_index);
    }
  };

  // Start traversal from root
  traverse_node(scene->mRootNode, -1);

  // Third pass: calculate global transforms
  std::function<void(int, const glm::mat4&)> calculate_global_transforms =
    [&](int bone_index, const glm::mat4& parent_transform) {
      if (bone_index < 0 || bone_index >= static_cast<int>(model.bones.size())) {
        return;
      }

      Bone& bone = model.bones[bone_index];
      bone.global_transform = parent_transform * bone.local_transform;

      // Recursively update children
      for (int child_index : bone.child_indices) {
        calculate_global_transforms(child_index, bone.global_transform);
      }
    };

  // Calculate global transforms for all root bones (bones with parent_index == -1)
  glm::mat4 identity = glm::mat4(1.0f);
  for (size_t i = 0; i < model.bones.size(); i++) {
    if (model.bones[i].parent_index == -1) {
      calculate_global_transforms(static_cast<int>(i), identity);
    }
  }

  model.has_skeleton = true;
  LOG_INFO("[SKELETON] Successfully loaded skeleton with {} bones", model.bones.size());
}

bool load_model(const std::string& filepath, Model& model, TextureManager& texture_manager) {
  // Clean up previous model
  cleanup_model(model);

  // Initialize model state
  model.path = filepath;
  model.has_no_geometry = false;

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

  // Process the scene hierarchy starting from root node
  glm::mat4 identity = glm::mat4(1.0f);
  process_node(scene->mRootNode, scene, model, identity);

  // Load skeleton data early (before geometry check) so animation-only files can load
  load_model_skeleton(scene, model);

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
  if (!model.vertices.empty()) {
    model.min_bounds = aiVector3D(FLT_MAX, FLT_MAX, FLT_MAX);
    model.max_bounds = aiVector3D(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (size_t i = 0; i < model.vertices.size(); i += 8) { // 3 pos + 3 normal + 2 tex coords
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

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*) 0);
  glEnableVertexAttribArray(0);
  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*) (3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  // Texture coordinate attribute
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*) (6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);

  // Load all materials from the model
  load_model_materials(scene, filepath, model, texture_manager);

  LOG_DEBUG("[3D] Loaded {} materials", model.materials.size());

  model.loaded = true;
  return true;
}

void render_model(const Model& model, TextureManager& texture_manager, const Camera3D& camera) {
  if (!model.loaded)
    return;

  glUseProgram(texture_manager.get_preview_shader());

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
    glm::vec3(0.0f, -1.0f, 0.0f)             // Up vector (flipped to fix upside-down models)
  );

  // Set uniforms
  glUniformMatrix4fv(glGetUniformLocation(texture_manager.get_preview_shader(), "model"), 1, GL_FALSE, glm::value_ptr(model_matrix));
  glUniformMatrix4fv(glGetUniformLocation(texture_manager.get_preview_shader(), "view"), 1, GL_FALSE, glm::value_ptr(view_matrix));

  // Dynamic far clipping plane based on camera distance
  float far_plane = camera_distance * 2.0f; // 2x camera distance to ensure model is visible
  glUniformMatrix4fv(
    glGetUniformLocation(texture_manager.get_preview_shader(), "projection"), 1, GL_FALSE,
    glm::value_ptr(glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, far_plane))
  );

  // Light and material properties
  glUniform3f(glGetUniformLocation(texture_manager.get_preview_shader(), "lightPos"), camera_x, camera_y + 1.0f, camera_z);
  glUniform3f(glGetUniformLocation(texture_manager.get_preview_shader(), "viewPos"), camera_x, camera_y, camera_z);
  glUniform3f(glGetUniformLocation(texture_manager.get_preview_shader(), "lightColor"), 1.0f, 1.0f, 1.0f);

  // NEW: Render each mesh with its material
  glBindVertexArray(model.vao);

  if (model.meshes.empty()) {
    // Fallback: render as single mesh with first material if available
    if (!model.materials.empty()) {
      const Material& material = model.materials[0];
      if (material.has_texture && material.texture_id != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, material.texture_id);
        glUniform1i(glGetUniformLocation(texture_manager.get_preview_shader(), "diffuseTexture"), 0);
        glUniform1i(glGetUniformLocation(texture_manager.get_preview_shader(), "useTexture"), 1);
      }
      else {
        glUniform1i(glGetUniformLocation(texture_manager.get_preview_shader(), "useTexture"), 0);
        glUniform3fv(glGetUniformLocation(texture_manager.get_preview_shader(), "materialColor"), 1, &material.diffuse_color[0]);
      }
      // Always pass emissive color (will be zero if material has no emissive)
      glUniform3fv(glGetUniformLocation(texture_manager.get_preview_shader(), "emissiveColor"), 1, &material.emissive_color[0]);
    }
    else {
      glUniform1i(glGetUniformLocation(texture_manager.get_preview_shader(), "useTexture"), 0);
      glm::vec3 default_color(0.7f, 0.7f, 0.7f);
      glUniform3fv(glGetUniformLocation(texture_manager.get_preview_shader(), "materialColor"), 1, &default_color[0]);
      // No emissive for default material
      glm::vec3 no_emissive(0.0f, 0.0f, 0.0f);
      glUniform3fv(glGetUniformLocation(texture_manager.get_preview_shader(), "emissiveColor"), 1, &no_emissive[0]);
    }

    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(model.indices.size()), GL_UNSIGNED_INT, 0);
  }
  else {
    // Render each mesh with its correct material
    for (const auto& mesh : model.meshes) {
      if (mesh.material_index < model.materials.size()) {
        const Material& material = model.materials[mesh.material_index];

        // Bind texture or set material color
        if (material.has_texture && material.texture_id != 0) {
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, material.texture_id);
          glUniform1i(glGetUniformLocation(texture_manager.get_preview_shader(), "diffuseTexture"), 0);
          glUniform1i(glGetUniformLocation(texture_manager.get_preview_shader(), "useTexture"), 1);
        }
        else {
          glUniform1i(glGetUniformLocation(texture_manager.get_preview_shader(), "useTexture"), 0);
          glUniform3fv(glGetUniformLocation(texture_manager.get_preview_shader(), "materialColor"), 1, &material.diffuse_color[0]);
        }
        // Always pass emissive color for this material
        glUniform3fv(glGetUniformLocation(texture_manager.get_preview_shader(), "emissiveColor"), 1, &material.emissive_color[0]);

        // Draw this specific mesh (indices are already properly offset)
        glDrawElements(
          GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, (void*) (mesh.index_offset * sizeof(unsigned int))
        );
      }
    }
  }

  glBindVertexArray(0);
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

  // Diamond has 6 vertices: start point, end point, and 4 corners forming square base
  unsigned int base_idx = vertices.size() / 8;

  // Vertex 0: Start point (narrow end at parent)
  vertices.insert(vertices.end(), {start.x, start.y, start.z, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});

  // Vertex 1: End point (narrow end at child)
  vertices.insert(vertices.end(), {end.x, end.y, end.z, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});

  // Vertices 2-5: Four corners forming a perfect square base (wide part near parent)
  // Using right and forward vectors ensures perpendicularity and equal spacing
  glm::vec3 corners[4] = {
    base_pos + right * width + forward * width,   // Corner 0: +X +Z
    base_pos - right * width + forward * width,   // Corner 1: -X +Z
    base_pos - right * width - forward * width,   // Corner 2: -X -Z
    base_pos + right * width - forward * width    // Corner 3: +X -Z
  };

  for (int i = 0; i < 4; i++) {
    vertices.insert(vertices.end(), {corners[i].x, corners[i].y, corners[i].z, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
  }

  // Create triangles for both pyramids
  // First pyramid (start point to square base) - short pyramid near parent
  for (int i = 0; i < 4; i++) {
    indices.push_back(base_idx + 0);           // Start point
    indices.push_back(base_idx + 2 + i);       // Current corner
    indices.push_back(base_idx + 2 + ((i + 1) % 4)); // Next corner
  }

  // Second pyramid (square base to end point) - long pyramid pointing to child
  for (int i = 0; i < 4; i++) {
    indices.push_back(base_idx + 1);           // End point
    indices.push_back(base_idx + 2 + ((i + 1) % 4)); // Next corner (reversed winding)
    indices.push_back(base_idx + 2 + i);       // Current corner
  }

  // Calculate normals for lighting (approximate - one normal per vertex)
  for (size_t i = base_idx * 8; i < vertices.size(); i += 8) {
    glm::vec3 pos(vertices[i], vertices[i + 1], vertices[i + 2]);
    glm::vec3 normal = glm::normalize(pos - base_pos);
    vertices[i + 3] = normal.x;
    vertices[i + 4] = normal.y;
    vertices[i + 5] = normal.z;
  }
}

void render_skeleton(const Model& model, const Camera3D& camera, TextureManager& texture_manager) {
  if (!model.has_skeleton || model.bones.empty()) {
    return;
  }

  // Use dedicated skeleton shader with directional lighting
  glUseProgram(texture_manager.get_skeleton_shader());

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
    glm::vec3(0.0f, -1.0f, 0.0f)
  );

  float far_plane = camera_distance * 2.0f;
  glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, far_plane);

  // Set uniforms for skeleton shader
  glUniformMatrix4fv(glGetUniformLocation(texture_manager.get_skeleton_shader(), "model"), 1, GL_FALSE, glm::value_ptr(model_matrix));
  glUniformMatrix4fv(glGetUniformLocation(texture_manager.get_skeleton_shader(), "view"), 1, GL_FALSE, glm::value_ptr(view_matrix));
  glUniformMatrix4fv(glGetUniformLocation(texture_manager.get_skeleton_shader(), "projection"), 1, GL_FALSE, glm::value_ptr(projection));

  // Set directional lighting for skeleton
  glm::vec3 light_direction = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
  glUniform3f(glGetUniformLocation(texture_manager.get_skeleton_shader(), "lightDir"), light_direction.x, light_direction.y, light_direction.z);
  glUniform3f(glGetUniformLocation(texture_manager.get_skeleton_shader(), "lightColor"), 1.0f, 1.0f, 1.0f);

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
    if ((Config::SKELETON_HIDE_CTRL_BONES && bone.name.find("Ctrl") != std::string::npos) ||
        (Config::SKELETON_HIDE_IK_BONES && bone.name.find("IK") != std::string::npos) ||
        (Config::SKELETON_HIDE_ROLL_BONES && bone.name.find("Roll") != std::string::npos) ||
        (Config::SKELETON_HIDE_ROOT_CHILDREN && parent_bone.name == "Root")) {
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
  glm::vec3 skeleton_color(1.0f, 1.0f, 1.0f);
  glUniform3fv(glGetUniformLocation(texture_manager.get_preview_shader(), "materialColor"), 1, &skeleton_color[0]);
  glm::vec3 no_emissive(0.0f, 0.0f, 0.0f);
  glUniform3fv(glGetUniformLocation(texture_manager.get_preview_shader(), "emissiveColor"), 1, &no_emissive[0]);

  // Draw bone geometry
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(bone_indices.size()), GL_UNSIGNED_INT, 0);

  // Cleanup
  glBindVertexArray(0);
  glDeleteBuffers(1, &bone_vbo);
  glDeleteBuffers(1, &bone_ebo);
  glDeleteVertexArrays(1, &bone_vao);
}

void cleanup_model(Model& model) {
  if (model.loaded) {
    glDeleteVertexArrays(1, &model.vao);
    glDeleteBuffers(1, &model.vbo);
    glDeleteBuffers(1, &model.ebo);

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
    model.has_skeleton = false;
    model.loaded = false;
  }
}

void set_current_model(Model& current_model, const Model& model) {
  // Clean up previous model
  cleanup_model(current_model);

  // Copy the new model
  current_model = model;
}

const Model& get_current_model(const Model& current_model) {
  return current_model;
}

void render_3d_preview(int width, int height, const Model& model, TextureManager& texture_manager, const Camera3D& camera) {
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
    Theme::BACKGROUND_LIGHT_GRAY.x, Theme::BACKGROUND_LIGHT_GRAY.y, Theme::BACKGROUND_LIGHT_GRAY.z,
    Theme::BACKGROUND_LIGHT_GRAY.w
  );
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (model.loaded) {
    render_model(model, texture_manager, camera);

    // Render skeleton overlay if present
    if (model.has_skeleton) {
      render_skeleton(model, camera, texture_manager);
    }
  }

  // Unbind framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

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
