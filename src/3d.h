#pragma once

#include "glad/glad.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <memory>
#include "theme.h"

// 3D Preview global variables
extern bool g_preview_initialized;
extern unsigned int g_preview_vao;
extern unsigned int g_preview_vbo;
extern unsigned int g_preview_shader;
extern unsigned int g_preview_texture;
extern unsigned int g_preview_depth_texture;
extern unsigned int g_preview_framebuffer;

// Model data structure
struct Model {
  std::vector<float> vertices;
  std::vector<unsigned int> indices;
  unsigned int vao = 0;
  unsigned int vbo = 0;
  unsigned int ebo = 0;
  unsigned int texture_id = 0; // OpenGL texture ID
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
unsigned int load_texture_for_model(const std::string& filepath);
