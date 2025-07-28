#include "3d.h"
#include <iostream>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "stb_image.h"
#include "texture_manager.h"
#include "theme.h"

// 3D Preview global variables are now managed by TextureManager

// 3D Preview no longer needs a global current model
// Model state is now managed by the caller

// Vertex shader source (updated for 3D models with texture support)
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

// Fragment shader source (updated for 3D models with multi-material support)
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

    vec3 result = (ambient + diffuse + fillLight + specular + rimLight) * objectColor;
    FragColor = vec4(result, 1.0);
}
)";

// Helper function to get base path
std::string getBasePath(const std::string& path) {
  size_t pos = path.find_last_of("\\/");
  return (std::string::npos == pos) ? "" : path.substr(0, pos + 1);
}


// Load all materials from model
void load_model_materials(const aiScene* scene, const std::string& model_path, std::vector<Material>& materials, TextureManager& texture_manager) {
  materials.clear();

  if (!scene || scene->mNumMaterials == 0) {
    std::cout << "No materials found in model" << std::endl;
    return;
  }

  // Get base path like the Assimp sample does
  std::string basepath = getBasePath(model_path);

  // Process ALL materials (not just the first one with a texture)
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

    // Check for diffuse textures first
    unsigned int diffuse_count = ai_material->GetTextureCount(aiTextureType_DIFFUSE);

    // Try to load texture
    for (unsigned int texIndex = 0; texIndex < diffuse_count; texIndex++) {
      aiString texture_path;
      aiReturn texFound = ai_material->GetTexture(aiTextureType_DIFFUSE, texIndex, &texture_path);

      if (texFound == AI_SUCCESS) {
        std::string filename = texture_path.C_Str();

        // Try to load the texture
        std::string fileloc = basepath + filename;
        if (std::filesystem::exists(fileloc)) {
          material.texture_id = texture_manager.load_texture_for_model(fileloc);
          if (material.texture_id != 0) {
            material.has_texture = true;
            break; // Use first successful texture
          }
        }
        else {
          // Try alternative path
          std::filesystem::path alt_path = std::filesystem::path(model_path).parent_path() / filename;
          if (std::filesystem::exists(alt_path)) {
            material.texture_id = texture_manager.load_texture_for_model(alt_path.string());
            if (material.texture_id != 0) {
              material.has_texture = true;
              break;
            }
          }
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

    material.diffuse_color = glm::vec3(diffuse_color.r, diffuse_color.g, diffuse_color.b);
    material.ambient_color = glm::vec3(ambient_color.r, ambient_color.g, ambient_color.b);
    material.specular_color = glm::vec3(specular_color.r, specular_color.g, specular_color.b);

    // Check if material color is black (0,0,0) and set a default if so
    if (material.diffuse_color.x == 0.0f && material.diffuse_color.y == 0.0f && material.diffuse_color.z == 0.0f) {
      material.diffuse_color = glm::vec3(0.8f, 0.8f, 0.8f);
    }

    // If no texture, create solid color texture
    if (!material.has_texture) {
      material.texture_id =
        texture_manager.create_solid_color_texture(material.diffuse_color.x, material.diffuse_color.y, material.diffuse_color.z);
    }

    materials.push_back(material);
  }
}

// Forward declarations
void process_node(aiNode* node, const aiScene* scene, Model& model, glm::mat4 parent_transform);
void process_mesh(aiMesh* mesh, const aiScene* scene, Model& model, glm::mat4 transform);
unsigned int load_model_texture(const aiScene* scene, const std::string& model_path);

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

bool load_model(const std::string& filepath, Model& model, TextureManager& texture_manager) {
  // Clean up previous model
  cleanup_model(model);

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(
    filepath, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace
  );

  if (!scene || !scene->mRootNode) {
    std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
    return false;
  }
  
  // Handle incomplete scenes (common with FBX files containing animations)
  if (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) {
    std::cout << "WARNING: Scene marked as incomplete (possibly due to animations), but proceeding with mesh data" << std::endl;
  }

  // Process the scene hierarchy starting from root node
  glm::mat4 identity = glm::mat4(1.0f);
  process_node(scene->mRootNode, scene, model, identity);
  
  // Check if the model has any visible geometry
  if (model.vertices.empty() || model.indices.empty()) {
    std::cout << "WARNING: Model has no visible geometry (possibly animation-only FBX)" << std::endl;
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

  // Create OpenGL buffers
  glGenVertexArrays(1, &model.vao);
  glGenBuffers(1, &model.vbo);
  glGenBuffers(1, &model.ebo);
  
  if (model.vao == 0 || model.vbo == 0 || model.ebo == 0) {
    std::cout << "ERROR: Failed to generate OpenGL buffers!" << std::endl;
    return false;
  }

  glBindVertexArray(model.vao);

  glBindBuffer(GL_ARRAY_BUFFER, model.vbo);
  glBufferData(GL_ARRAY_BUFFER, model.vertices.size() * sizeof(float), model.vertices.data(), GL_STATIC_DRAW);
  
  // Check for OpenGL errors
  GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    std::cout << "ERROR: OpenGL error after vertex buffer creation: " << error << std::endl;
    return false;
  }

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model.ebo);
  glBufferData(
    GL_ELEMENT_ARRAY_BUFFER, model.indices.size() * sizeof(unsigned int), model.indices.data(), GL_STATIC_DRAW
  );
  
  error = glGetError();
  if (error != GL_NO_ERROR) {
    std::cout << "ERROR: OpenGL error after index buffer creation: " << error << std::endl;
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
  load_model_materials(scene, filepath, model.materials, texture_manager);

  model.path = filepath;  // Store the path
  model.loaded = true;

  return true;
}

void render_model(const Model& model, TextureManager& texture_manager) {
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

  // Calculate camera distance based on model size
  // We want the model to fit nicely in the view, so move camera back based on size
  float camera_distance = max_size * 1.5f; // 1.5x the model size for good framing
  // if (camera_distance < 150.0f)
  //   camera_distance = 150.0f; // Minimum distance

  // Position camera at an angle for a nicer preview - looking down from above and to the side
  float camera_x = camera_distance * 0.7f; // 45 degrees horizontally
  float camera_y = camera_distance * 0.5f; // 30 degrees above horizontal
  float camera_z = camera_distance * 0.7f; // 45 degrees horizontally

  glm::mat4 view_matrix = glm::lookAt(
    glm::vec3(camera_x, camera_y, camera_z), // Camera position - angled view from above
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
    }
    else {
      glUniform1i(glGetUniformLocation(texture_manager.get_preview_shader(), "useTexture"), 0);
      glm::vec3 default_color(0.7f, 0.7f, 0.7f);
      glUniform3fv(glGetUniformLocation(texture_manager.get_preview_shader(), "materialColor"), 1, &default_color[0]);
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

        // Draw this specific mesh (indices are already properly offset)
        glDrawElements(
          GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, (void*) (mesh.index_offset * sizeof(unsigned int))
        );
      }
    }
  }

  glBindVertexArray(0);
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

void render_3d_preview(int width, int height, const Model& model, TextureManager& texture_manager) {
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

  // Render the model if loaded, otherwise render a simple colored triangle
  if (model.loaded) {
    render_model(model, texture_manager);
  }
  else {
    // Fallback: render a simple colored triangle
    glUseProgram(texture_manager.get_preview_shader());

    // Set up matrices for triangle
    glm::mat4 model_matrix = glm::mat4(1.0f);
    glm::mat4 view_matrix =
      glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    glm::mat4 projection_matrix = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 1000.0f);

    glUniformMatrix4fv(glGetUniformLocation(texture_manager.get_preview_shader(), "model"), 1, GL_FALSE, glm::value_ptr(model_matrix));
    glUniformMatrix4fv(glGetUniformLocation(texture_manager.get_preview_shader(), "view"), 1, GL_FALSE, glm::value_ptr(view_matrix));
    glUniformMatrix4fv(
      glGetUniformLocation(texture_manager.get_preview_shader(), "projection"), 1, GL_FALSE, glm::value_ptr(projection_matrix)
    );

    // Set lighting uniforms for fallback triangle
    glUniform3f(glGetUniformLocation(texture_manager.get_preview_shader(), "lightPos"), 2.0f, 2.0f, 3.0f);
    glUniform3f(glGetUniformLocation(texture_manager.get_preview_shader(), "viewPos"), 0.0f, 0.0f, 3.0f);
    glUniform3f(glGetUniformLocation(texture_manager.get_preview_shader(), "lightColor"), 1.0f, 1.0f, 1.0f);
    glUniform1i(glGetUniformLocation(texture_manager.get_preview_shader(), "useTexture"), 0);

    // Create a simple triangle
    float vertices[] = {
      -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, // bottom left
      0.5f,  -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, // bottom right
      0.0f,  0.5f,  0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f  // top
    };

    unsigned int vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*) 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*) (3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*) (6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
  }

  // Unbind framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
