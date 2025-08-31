#pragma once

#include "glad/glad.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

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
  bool has_missing_texture_files = false; // True if material references texture files that don't exist
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

  // Bounds
  aiVector3D min_bounds;
  aiVector3D max_bounds;

  // Model metadata
  std::string path = ""; // Path to the loaded model file
  bool loaded = false;
};

// Camera state for 3D preview interaction
struct Camera3D {
  float rotation_x = 30.0f;  // Initial rotation around X axis (up/down)
  float rotation_y = 45.0f;  // Initial rotation around Y axis (left/right)
  float zoom = 1.0f;          // Zoom factor (1.0 = default)
  
  // Mouse interaction state
  bool is_dragging = false;
  float last_mouse_x = 0.0f;
  float last_mouse_y = 0.0f;
  
  // Reset to default view
  void reset() {
    rotation_x = 30.0f;
    rotation_y = 45.0f;
    zoom = 1.0f;
  }
};

// 3D Preview functions (now handled by TextureManager)
void render_3d_preview(int width, int height, const Model& model, TextureManager& texture_manager, const Camera3D& camera);
bool load_model(const std::string& filepath, Model& model, TextureManager& texture_manager);
void render_model(const Model& model, TextureManager& texture_manager, const Camera3D& camera);
void cleanup_model(Model& model);
void set_current_model(Model& current_model, const Model& model);
const Model& get_current_model(const Model& current_model);

// Material functions (now using TextureManager)
void load_model_materials(const aiScene* scene, const std::string& model_path, Model& model, TextureManager& texture_manager);
