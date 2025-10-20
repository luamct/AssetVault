#pragma once

#include "glad/glad.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <unordered_map>

// Forward declaration
class TextureManager;

// Material data structure
struct Material {
  std::string name;            // Material name from file
  unsigned int texture_id = 0; // OpenGL texture ID (0 = no texture)

  // Material color properties (for fallback when no texture)
  glm::vec3 diffuse_color = glm::vec3(0.8f, 0.8f, 0.8f);
  glm::vec3 ambient_color = glm::vec3(0.2f, 0.2f, 0.2f);
  glm::vec3 specular_color = glm::vec3(0.0f, 0.0f, 0.0f);
  glm::vec3 emissive_color = glm::vec3(0.0f, 0.0f, 0.0f);  // Glow/emission color
  float shininess = 0.0f;
  float emissive_intensity = 1.0f;  // Multiplier for emissive color

  // Flags
  bool has_texture = false;
  bool has_diffuse_color = false;
  bool has_emissive = false; // True if material has emissive properties
};

// Mesh data structure
struct Mesh {
  unsigned int material_index; // Index into Model's materials vector
  unsigned int vertex_offset;  // Start position in vertex buffer (in vertices)
  unsigned int vertex_count;   // Number of vertices
  unsigned int index_offset;   // Start position in index buffer
  unsigned int index_count;    // Number of indices
  std::string name;            // Mesh name (for debugging)
};

// Bone data structure for skeletal animation
struct Bone {
  std::string name;                  // Bone name
  glm::mat4 offset_matrix;           // Transforms from mesh space to bone space
  glm::mat4 local_transform;         // Local transformation relative to parent
  glm::mat4 global_transform;        // World space transformation
  int parent_index;                  // Index of parent bone (-1 for root)
  std::vector<int> child_indices;    // Indices of child bones
  glm::vec3 rest_position = glm::vec3(0.0f); // Rest pose translation
  glm::quat rest_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Rest pose rotation
  glm::vec3 rest_scale = glm::vec3(1.0f);    // Rest pose scale
  int skeleton_node_index = -1;      // Index into Model::skeleton_nodes for fast lookup
};

struct SkeletonNode {
  std::string name_raw;              // Exact Assimp node name (namespace/helpers intact)
  std::string name;                  // Normalized name (namespace stripped)
  glm::mat4 rest_local_transform = glm::mat4(1.0f);    // Local transform at rest
  glm::mat4 rest_global_transform = glm::mat4(1.0f);   // Global transform at rest
  glm::vec3 rest_position = glm::vec3(0.0f);           // Rest translation component
  glm::quat rest_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Rest rotation component
  glm::vec3 rest_scale = glm::vec3(1.0f);              // Rest scale component
  int parent_index = -1;                                // Parent node index (-1 for root)
  std::vector<int> child_indices;                      // Children node indices
  int bone_index = -1;                                  // Associated bone (-1 if none)
  bool is_bone = false;                                 // True if this node is the primary bone transform
  bool is_helper = false;                               // True if this node is a helper affecting a bone
};

struct AnimationKeyframeVec3 {
  double time = 0.0;   // In ticks
  glm::vec3 value = glm::vec3(0.0f);
};

struct AnimationKeyframeQuat {
  double time = 0.0;   // In ticks
  glm::quat value = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

struct AnimationChannel {
  int node_index = -1;  // Skeleton node affected by this channel
  std::vector<AnimationKeyframeVec3> position_keys;
  std::vector<AnimationKeyframeQuat> rotation_keys;
  std::vector<AnimationKeyframeVec3> scaling_keys;
};

struct AnimationClip {
  std::string name;
  double duration = 0.0;          // Duration in ticks
  double ticks_per_second = 0.0;  // Conversion factor from seconds to ticks
  std::vector<AnimationChannel> channels;

  bool is_valid() const {
    return duration > 0.0 && !channels.empty();
  }
};

// Model data structure
struct Model {
  // Geometry data
  std::vector<float> vertices;
  std::vector<unsigned int> indices;
  unsigned int vao = 0;
  unsigned int vbo = 0;
  unsigned int ebo = 0;

  // Multi-material support
  std::vector<Material> materials; // All materials in this model
  std::vector<Mesh> meshes;        // All meshes with material associations

  // Skeletal data
  std::vector<Bone> bones;         // Bone hierarchy for skeletal animation
  bool has_skeleton = false;       // True if model contains skeletal data
  std::unordered_map<std::string, int> bone_lookup; // Bone name -> index mapping

  // Animation data
  std::vector<AnimationClip> animations;
  std::vector<glm::mat4> animated_local_transforms; // Scratch buffer for pose evaluation
  bool animation_playing = false;
  double animation_time = 0.0;     // Accumulated time in seconds for active clip
  size_t active_animation = 0;     // Currently selected clip index

  // Skeleton node graph (includes helpers and other transform parents)
  std::vector<SkeletonNode> skeleton_nodes;
  std::unordered_map<std::string, int> skeleton_node_lookup; // Raw node name -> node index mapping
  std::vector<glm::mat4> animated_node_local_transforms;  // Scratch locals per skeleton node
  std::vector<glm::mat4> animated_node_global_transforms; // Scratch globals per skeleton node

  // Bounds
  aiVector3D min_bounds;
  aiVector3D max_bounds;

  // Model metadata
  std::string path = ""; // Path to the loaded model file
  bool loaded = false;
  bool has_no_geometry = false; // True for animation-only files (no renderable meshes)
};

// Camera state for 3D preview interaction
struct Camera3D {
  float rotation_x = 30.0f;   // Initial rotation around X axis (up/down)
  float rotation_y = -45.0f;  // Initial rotation around Y axis (left/right)
  float zoom = 1.0f;           // Zoom factor (1.0 = default)

  // Mouse interaction state
  bool is_dragging = false;
  float last_mouse_x = 0.0f;
  float last_mouse_y = 0.0f;

  // Reset to default view
  void reset() {
    rotation_x = 30.0f;
    rotation_y = -45.0f;
    zoom = 1.0f;
  }
};

// 3D Preview functions (now handled by TextureManager)
void render_3d_preview(int width, int height, Model& model, TextureManager& texture_manager, const Camera3D& camera, float delta_time);
bool load_model(const std::string& filepath, Model& model, TextureManager& texture_manager);
void render_model(const Model& model, TextureManager& texture_manager, const Camera3D& camera);
void cleanup_model(Model& model);
void set_current_model(Model& current_model, const Model& model);
const Model& get_current_model(const Model& current_model);

// Extract texture paths referenced by a 3D model
// Returns vector of texture paths (relative to model directory) used by the model
// Useful for drag-and-drop to include all necessary texture files
std::vector<std::string> extract_model_texture_paths(const std::string& model_path);

// Material functions (now using TextureManager)
void load_model_materials(const aiScene* scene, const std::string& model_path, Model& model, TextureManager& texture_manager);

// OpenGL state setup for consistent 3D rendering across contexts
void setup_3d_rendering_state();

// Skeleton rendering for models with bone data
void render_skeleton(const Model& model, const Camera3D& camera, TextureManager& texture_manager);

// Debug visualization
// Render 3D coordinate axes at origin (X=red, Y=green, Z=blue) with arrow heads
// Scale parameter adjusts axis length relative to scene
// Uses provided view and projection matrices to ensure consistency with model rendering
void render_debug_axes(float scale, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& light_direction);

// Shader management functions
// Initialize 3D shaders by loading from external shader files
// Must be called after OpenGL context is created
// Returns true on success, false on failure
bool initialize_3d_shaders();

// Cleanup 3D shader programs (call before destroying OpenGL context)
void cleanup_3d_shaders();

// Shader loading utilities
// Load shader source code from file and compile/link into OpenGL shader program
// Returns shader program ID (0 on failure)
unsigned int load_shader_program(const std::string& vertex_path, const std::string& fragment_path);
