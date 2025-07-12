#include "3d.h"
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "stb_image.h"

// 3D Preview global variables
bool g_preview_initialized = false;
unsigned int g_preview_vao = 0;
unsigned int g_preview_vbo = 0;
unsigned int g_preview_shader = 0;
unsigned int g_preview_texture = 0;
unsigned int g_preview_depth_texture = 0;
unsigned int g_preview_framebuffer = 0;

// Currently loaded model
static Model g_current_model;
static bool g_model_loaded = false;

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

// Fragment shader source (updated for 3D models with improved lighting)
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

out vec4 FragColor;

void main()
{
    // Sample texture color or use default white
    vec3 objectColor = useTexture ? texture(diffuseTexture, TexCoord).rgb : vec3(0.7, 0.7, 0.7);

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

bool initialize_3d_preview() {
  // Create shader program
  unsigned int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex_shader, 1, &vertex_shader_source, nullptr);
  glCompileShader(vertex_shader);

  // Check for shader compile errors
  int success;
  char info_log[512];
  glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vertex_shader, 512, nullptr, info_log);
    std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << info_log << std::endl;
    return false;
  }

  unsigned int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment_shader, 1, &fragment_shader_source, nullptr);
  glCompileShader(fragment_shader);

  glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fragment_shader, 512, nullptr, info_log);
    std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << info_log << std::endl;
    return false;
  }

  g_preview_shader = glCreateProgram();
  glAttachShader(g_preview_shader, vertex_shader);
  glAttachShader(g_preview_shader, fragment_shader);
  glLinkProgram(g_preview_shader);

  glGetProgramiv(g_preview_shader, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(g_preview_shader, 512, nullptr, info_log);
    std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << info_log << std::endl;
    return false;
  }

  // Clean up shaders
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  // Create framebuffer
  glGenFramebuffers(1, &g_preview_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, g_preview_framebuffer);

  // Create color texture
  glGenTextures(1, &g_preview_texture);
  glBindTexture(GL_TEXTURE_2D, g_preview_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 800, 800, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_preview_texture, 0);

  // Create depth texture
  glGenTextures(1, &g_preview_depth_texture);
  glBindTexture(GL_TEXTURE_2D, g_preview_depth_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, 800, 800, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, g_preview_depth_texture, 0);

  // Check framebuffer completeness
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
    return false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Enable depth testing
  glEnable(GL_DEPTH_TEST);

  g_preview_initialized = true;
  std::cout << "3D preview initialized successfully!" << std::endl;
  return true;
}

void cleanup_3d_preview() {
  if (g_preview_initialized) {
    cleanup_model(g_current_model);
    glDeleteProgram(g_preview_shader);
    glDeleteTextures(1, &g_preview_texture);
    glDeleteTextures(1, &g_preview_depth_texture);
    glDeleteFramebuffers(1, &g_preview_framebuffer);
    g_preview_initialized = false;
  }
}

// Function to load texture for 3D models
unsigned int load_texture_for_model(const std::string& filepath) {
  int width, height, channels;
  unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
  if (!data) {
    std::cout << "Failed to load texture: " << filepath << std::endl;
    return 0;
  }

  unsigned int texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Set texture parameters
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // Upload texture data
  GLenum format = GL_RGB;
  if (channels == 4)
    format = GL_RGBA;
  else if (channels == 1)
    format = GL_RED;

  glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);

  stbi_image_free(data);
  std::cout << "Texture loaded successfully: " << filepath << std::endl;
  return texture_id;
}

// Forward declarations
void process_node(aiNode* node, const aiScene* scene, Model& model, glm::mat4 parent_transform);
void process_mesh(aiMesh* mesh, const aiScene* scene, Model& model, glm::mat4 transform);

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
  std::cout << "Processing mesh: " << mesh->mName.C_Str() << std::endl;
  std::cout << "  Vertices: " << mesh->mNumVertices << std::endl;
  std::cout << "  Faces: " << mesh->mNumFaces << std::endl;

  // Calculate vertex offset for this mesh
  unsigned int vertex_offset = static_cast<unsigned int>(model.vertices.size() / 8);

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
    } else {
      model.vertices.push_back(0.0f);
      model.vertices.push_back(0.0f);
      model.vertices.push_back(1.0f);
    }

    // Texture coordinates (no transformation needed)
    if (mesh->mTextureCoords[0]) {
      model.vertices.push_back(mesh->mTextureCoords[0][i].x);
      model.vertices.push_back(mesh->mTextureCoords[0][i].y);
    } else {
      model.vertices.push_back(0.0f);
      model.vertices.push_back(0.0f);
    }
  }

  // Process indices with proper offset
  for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
    aiFace face = mesh->mFaces[i];
    for (unsigned int j = 0; j < face.mNumIndices; j++) {
      model.indices.push_back(face.mIndices[j] + vertex_offset);
    }
  }
}

bool load_model(const std::string& filepath, Model& model) {
  // Clean up previous model
  cleanup_model(model);

  std::cout << "=== Loading Model ===" << std::endl;
  std::cout << "Filepath: " << filepath << std::endl;

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(filepath, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs |
                                                         aiProcess_CalcTangentSpace);

  if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
    std::cout << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
    return false;
  }

  std::cout << "Scene loaded successfully!" << std::endl;
  std::cout << "Number of meshes: " << scene->mNumMeshes << std::endl;

  // Process the scene hierarchy starting from root node
  glm::mat4 identity = glm::mat4(1.0f);
  process_node(scene->mRootNode, scene, model, identity);

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

  glBindVertexArray(model.vao);

  glBindBuffer(GL_ARRAY_BUFFER, model.vbo);
  glBufferData(GL_ARRAY_BUFFER, model.vertices.size() * sizeof(float), model.vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, model.indices.size() * sizeof(unsigned int), model.indices.data(),
               GL_STATIC_DRAW);

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  // Texture coordinate attribute
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);

  // Load texture
  std::string texture_path = "assets/Car Kit/Models/OBJ format/Textures/colormap.png";
  model.texture_id = load_texture_for_model(texture_path);

  model.loaded = true;
  std::cout << "Model loaded successfully: " << filepath << std::endl;
  std::cout << "Total vertices: " << model.vertices.size() / 8 << ", Total indices: " << model.indices.size()
            << std::endl;
  std::cout << "Model bounds - Min: (" << model.min_bounds.x << ", " << model.min_bounds.y << ", " << model.min_bounds.z
            << ")" << std::endl;
  std::cout << "Model bounds - Max: (" << model.max_bounds.x << ", " << model.max_bounds.y << ", " << model.max_bounds.z
            << ")" << std::endl;
  std::cout << "===================" << std::endl;

  return true;
}

void render_model(const Model& model) {
  if (!model.loaded)
    return;

  // Only print debug info once per model selection
  static bool debug_printed = false;
  static size_t last_vertex_count = 0;
  bool should_debug = !debug_printed || (model.vertices.size() != last_vertex_count);
  if (should_debug) {
    debug_printed = true;
    last_vertex_count = model.vertices.size();
  }

  glUseProgram(g_preview_shader);

  // Set up matrices
  glm::mat4 model_matrix = glm::mat4(1.0f);

  // Center and position the model to ensure it's always visible
  aiVector3D center = (model.min_bounds + model.max_bounds) * 0.5f;
  aiVector3D size = model.max_bounds - model.min_bounds;
  float max_size = std::max({size.x, size.y, size.z});

  // Just center the model at the origin
  model_matrix = glm::translate(model_matrix, glm::vec3(-center.x, -center.y, -center.z));

  // Calculate camera distance based on model size
  // We want the model to fit nicely in the view, so move camera back based on size
  float camera_distance = max_size * 1.5f; // 1.5x the model size for good framing
  if (camera_distance < 150.0f)
    camera_distance = 150.0f; // Minimum distance

  // Position camera at an angle for a nicer preview - looking down from above and to the side
  float camera_x = camera_distance * 0.7f; // 45 degrees horizontally
  float camera_y = camera_distance * 0.5f; // 30 degrees above horizontal
  float camera_z = camera_distance * 0.7f; // 45 degrees horizontally

  glm::mat4 view_matrix =
      glm::lookAt(glm::vec3(camera_x, camera_y, camera_z), // Camera position - angled view from above
                  glm::vec3(0.0f, 0.0f, 0.0f),             // Look at origin where model is centered
                  glm::vec3(0.0f, -1.0f, 0.0f)             // Up vector (flipped to fix upside-down models)
      );

  // Debug output (only once per model)
  if (should_debug) {
    std::cout << "=== Model Rendering Debug ===" << std::endl;
    std::cout << "Model bounds - Min: (" << model.min_bounds.x << ", " << model.min_bounds.y << ", "
              << model.min_bounds.z << ")" << std::endl;
    std::cout << "Model bounds - Max: (" << model.max_bounds.x << ", " << model.max_bounds.y << ", "
              << model.max_bounds.z << ")" << std::endl;
    std::cout << "Model center: (" << center.x << ", " << center.y << ", " << center.z << ")" << std::endl;
    std::cout << "Model size: (" << size.x << ", " << size.y << ", " << size.z << ")" << std::endl;
    std::cout << "Max size: " << max_size << std::endl;
    std::cout << "Camera distance: " << camera_distance << std::endl;
    std::cout << "Camera position: (" << camera_x << ", " << camera_y << ", " << camera_z << ")" << std::endl;
    std::cout << "Camera target: (0.0, 0.0, 0.0)" << std::endl;
    std::cout << "Model translation: (" << -center.x << ", " << -center.y << ", " << -center.z << ")" << std::endl;
    std::cout << "Vertices count: " << model.vertices.size() / 8 << std::endl;
    std::cout << "Indices count: " << model.indices.size() << std::endl;
    std::cout << "Far clipping plane: " << camera_distance * 2.0f << std::endl;
    std::cout << "Model should be perfectly centered at origin and visible!" << std::endl;
  }

  // Set uniforms
  glUniformMatrix4fv(glGetUniformLocation(g_preview_shader, "model"), 1, GL_FALSE, glm::value_ptr(model_matrix));
  glUniformMatrix4fv(glGetUniformLocation(g_preview_shader, "view"), 1, GL_FALSE, glm::value_ptr(view_matrix));

  // Dynamic far clipping plane based on camera distance
  float far_plane = camera_distance * 2.0f; // 2x camera distance to ensure model is visible
  glUniformMatrix4fv(glGetUniformLocation(g_preview_shader, "projection"), 1, GL_FALSE,
                     glm::value_ptr(glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, far_plane)));

  // Light and material properties
  glUniform3f(glGetUniformLocation(g_preview_shader, "lightPos"), camera_x, camera_y + 1.0f, camera_z);
  glUniform3f(glGetUniformLocation(g_preview_shader, "viewPos"), camera_x, camera_y, camera_z);
  glUniform3f(glGetUniformLocation(g_preview_shader, "lightColor"), 1.0f, 1.0f, 1.0f);

  // Bind texture if available
  if (model.texture_id != 0) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, model.texture_id);
    glUniform1i(glGetUniformLocation(g_preview_shader, "diffuseTexture"), 0);
    glUniform1i(glGetUniformLocation(g_preview_shader, "useTexture"), 1);
  } else {
    glUniform1i(glGetUniformLocation(g_preview_shader, "useTexture"), 0);
  }

  // Draw the model
  glBindVertexArray(model.vao);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(model.indices.size()), GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
}

void cleanup_model(Model& model) {
  if (model.loaded) {
    glDeleteVertexArrays(1, &model.vao);
    glDeleteBuffers(1, &model.vbo);
    glDeleteBuffers(1, &model.ebo);
    if (model.texture_id != 0) {
      glDeleteTextures(1, &model.texture_id);
      model.texture_id = 0;
    }
    model.vertices.clear();
    model.indices.clear();
    model.loaded = false;
  }
}

void set_current_model(const Model& model) {
  // Clean up previous model
  cleanup_model(g_current_model);

  // Copy the new model
  g_current_model = model;
  g_model_loaded = model.loaded;
}

void render_3d_preview(int width, int height) {
  if (!g_preview_initialized) {
    return;
  }

  // Update framebuffer size if needed
  static int last_fb_width = 0, last_fb_height = 0;
  if (width != last_fb_width || height != last_fb_height) {
    // Recreate framebuffer with new size
    glBindTexture(GL_TEXTURE_2D, g_preview_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, g_preview_depth_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8,
                 nullptr);
    last_fb_width = width;
    last_fb_height = height;
  }

  // Render to framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, g_preview_framebuffer);
  glViewport(0, 0, width, height);
  glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Render the model if loaded, otherwise render a simple colored triangle
  if (g_model_loaded && g_current_model.loaded) {
    render_model(g_current_model);
  } else {
    // Fallback: render a simple colored triangle
    glUseProgram(g_preview_shader);

    // Set up matrices for triangle
    glm::mat4 model_matrix = glm::mat4(1.0f);
    glm::mat4 view_matrix =
        glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    glm::mat4 projection_matrix = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 1000.0f);

    glUniformMatrix4fv(glGetUniformLocation(g_preview_shader, "model"), 1, GL_FALSE, glm::value_ptr(model_matrix));
    glUniformMatrix4fv(glGetUniformLocation(g_preview_shader, "view"), 1, GL_FALSE, glm::value_ptr(view_matrix));
    glUniformMatrix4fv(glGetUniformLocation(g_preview_shader, "projection"), 1, GL_FALSE,
                       glm::value_ptr(projection_matrix));

    // Set lighting uniforms for fallback triangle
    glUniform3f(glGetUniformLocation(g_preview_shader, "lightPos"), 2.0f, 2.0f, 3.0f);
    glUniform3f(glGetUniformLocation(g_preview_shader, "viewPos"), 0.0f, 0.0f, 3.0f);
    glUniform3f(glGetUniformLocation(g_preview_shader, "lightColor"), 1.0f, 1.0f, 1.0f);
    glUniform1i(glGetUniformLocation(g_preview_shader, "useTexture"), 0);

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

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
  }

  // Unbind framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
