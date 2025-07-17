#pragma once

#include "glad/glad.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

// 3D Preview global variables
extern bool g_preview_initialized;
extern unsigned int g_preview_shader;
extern unsigned int g_preview_texture;
extern unsigned int g_preview_depth_texture;
extern unsigned int g_preview_framebuffer;

// Material data structure
struct Material {
  std::string name;            // Material name from file
  unsigned int texture_id = 0; // OpenGL texture ID (0 = no texture)

  // Material color properties (for fallback when no texture)
  glm::vec3 diffuse_color = glm::vec3(0.8f, 0.8f, 0.8f);
  glm::vec3 ambient_color = glm::vec3(0.2f, 0.2f, 0.2f);
  glm::vec3 specular_color = glm::vec3(0.0f, 0.0f, 0.0f);
  float shininess = 0.0f;

  // Flags
  bool has_texture = false;
  bool has_diffuse_color = false;
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
  bool loaded = false;
};

// 3D Preview functions
bool initialize_3d_preview();
void cleanup_3d_preview();
void render_3d_preview(int width, int height);
bool load_model(const std::string& filepath, Model& model);
void render_model(const Model& model);
void cleanup_model(Model& model);
void set_current_model(const Model& model);
const Model& get_current_model();

// Material and texture functions
unsigned int load_texture_for_model(const std::string& filepath);
void load_model_materials(const aiScene* scene, const std::string& model_path, std::vector<Material>& materials);
unsigned int create_solid_color_texture(float r, float g, float b);
