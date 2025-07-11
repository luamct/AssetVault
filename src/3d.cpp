#ifdef _WIN32
#include <windows.h>
#endif

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <assimp/Importer.hpp>
#include <filesystem>
#include <iostream>

#include "3d.h"

// Vertex shader source
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

// Fragment shader source
const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec3 viewPos;

void main()
{
    // Ambient
    vec3 ambient = ambientColor * lightColor;

    // Diffuse
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // Specular
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = spec * lightColor;

    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, 1.0);
}
)";

// Model3D implementation
Model3D::Model3D()
    : loaded(false), totalVertices(0), totalTriangles(0), shaderProgram(0), vertexShader(0), fragmentShader(0),
      minBounds(FLT_MAX, FLT_MAX, FLT_MAX), maxBounds(-FLT_MAX, -FLT_MAX, -FLT_MAX) {}

Model3D::~Model3D() {
  cleanup();
}

bool Model3D::load(const std::string& filepath) {
  cleanup();

  this->filepath = filepath;

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(filepath, aiProcess_Triangulate | aiProcess_GenNormals |
                                                         aiProcess_CalcTangentSpace | aiProcess_FlipUVs);

  if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
    std::cerr << "Assimp error: " << importer.GetErrorString() << std::endl;
    return false;
  }

  processNode(scene->mRootNode, scene);
  setupShaders();

  loaded = true;
  return true;
}

void Model3D::processNode(aiNode* node, const aiScene* scene) {
  // Process all meshes in the node
  for (unsigned int i = 0; i < node->mNumMeshes; i++) {
    aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
    processMesh(mesh, scene);
  }

  // Process all children
  for (unsigned int i = 0; i < node->mNumChildren; i++) {
    processNode(node->mChildren[i], scene);
  }
}

void Model3D::processMesh(aiMesh* mesh, const aiScene* scene) {
  Mesh newMesh;

  // Process vertices
  for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
    Vertex vertex;

    // Position
    vertex.position.x = mesh->mVertices[i].x;
    vertex.position.y = mesh->mVertices[i].y;
    vertex.position.z = mesh->mVertices[i].z;

    // Update bounds
    minBounds.x = std::min(minBounds.x, vertex.position.x);
    minBounds.y = std::min(minBounds.y, vertex.position.y);
    minBounds.z = std::min(minBounds.z, vertex.position.z);
    maxBounds.x = std::max(maxBounds.x, vertex.position.x);
    maxBounds.y = std::max(maxBounds.y, vertex.position.y);
    maxBounds.z = std::max(maxBounds.z, vertex.position.z);

    // Normals
    if (mesh->HasNormals()) {
      vertex.normal.x = mesh->mNormals[i].x;
      vertex.normal.y = mesh->mNormals[i].y;
      vertex.normal.z = mesh->mNormals[i].z;
      newMesh.hasNormals = true;
    } else {
      vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
      newMesh.hasNormals = false;
    }

    // Texture coordinates
    if (mesh->mTextureCoords[0]) {
      vertex.texCoords.x = mesh->mTextureCoords[0][i].x;
      vertex.texCoords.y = mesh->mTextureCoords[0][i].y;
      newMesh.hasTexCoords = true;
    } else {
      vertex.texCoords = glm::vec2(0.0f, 0.0f);
      newMesh.hasTexCoords = false;
    }

    newMesh.vertices.push_back(vertex);
  }

  // Process indices
  for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
    aiFace face = mesh->mFaces[i];
    for (unsigned int j = 0; j < face.mNumIndices; j++) {
      newMesh.indices.push_back(face.mIndices[j]);
    }
  }

  setupMesh(newMesh);
  meshes.push_back(newMesh);

  totalVertices += newMesh.vertices.size();
  totalTriangles += newMesh.indices.size() / 3;
}

void Model3D::setupMesh(Mesh& mesh) {
  glGenVertexArrays(1, &mesh.vao);
  glGenBuffers(1, &mesh.vbo);
  glGenBuffers(1, &mesh.ebo);

  glBindVertexArray(mesh.vao);

  glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
  glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex), &mesh.vertices[0], GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), &mesh.indices[0], GL_STATIC_DRAW);

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
  glEnableVertexAttribArray(0);

  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
  glEnableVertexAttribArray(1);

  // Texture coordinate attribute
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);
}

void Model3D::setupShaders() {
  // Vertex shader
  vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
  glCompileShader(vertexShader);

  // Check for shader compile errors
  int success;
  char infoLog[512];
  glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
    std::cerr << "Vertex shader compilation failed: " << infoLog << std::endl;
  }

  // Fragment shader
  fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
  glCompileShader(fragmentShader);

  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
    std::cerr << "Fragment shader compilation failed: " << infoLog << std::endl;
  }

  // Shader program
  shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vertexShader);
  glAttachShader(shaderProgram, fragmentShader);
  glLinkProgram(shaderProgram);

  glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
    std::cerr << "Shader program linking failed: " << infoLog << std::endl;
  }

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
}

void Model3D::render() const {
  if (!loaded || shaderProgram == 0)
    return;

  // Note: shader program should be set by the renderer before calling this
  for (const auto& mesh : meshes) {
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
  }
}

void Model3D::cleanup() {
  for (auto& mesh : meshes) {
    glDeleteVertexArrays(1, &mesh.vao);
    glDeleteBuffers(1, &mesh.vbo);
    glDeleteBuffers(1, &mesh.ebo);
  }
  meshes.clear();

  if (shaderProgram != 0) {
    glDeleteProgram(shaderProgram);
    shaderProgram = 0;
  }

  loaded = false;
  totalVertices = 0;
  totalTriangles = 0;
}

// Camera3D implementation
Camera3D::Camera3D()
    : position(3.0f, 2.0f, 3.0f), front(0.0f, 0.0f, -1.0f), up(0.0f, 1.0f, 0.0f), worldUp(0.0f, 1.0f, 0.0f),
      yaw(-45.0f), pitch(-15.0f), movementSpeed(2.5f), mouseSensitivity(0.1f), zoom(45.0f), fov(45.0f), nearPlane(0.1f),
      farPlane(1000.0f), firstMouse(true), lastX(0.0f), lastY(0.0f) {
  updateCameraVectors();
}

void Camera3D::update(float deltaTime) {
  // Camera movement can be added here if needed
}

void Camera3D::handleMouse(float xoffset, float yoffset, bool constrainPitch) {
  xoffset *= mouseSensitivity;
  yoffset *= mouseSensitivity;

  yaw += xoffset;
  pitch += yoffset;

  if (constrainPitch) {
    if (pitch > 89.0f)
      pitch = 89.0f;
    if (pitch < -89.0f)
      pitch = -89.0f;
  }

  updateCameraVectors();
}

void Camera3D::handleScroll(float yoffset) {
  zoom -= yoffset;
  if (zoom < 1.0f)
    zoom = 1.0f;
  if (zoom > 45.0f)
    zoom = 45.0f;
}

glm::mat4 Camera3D::getViewMatrix() const {
  return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera3D::getProjectionMatrix() const {
  // Use a reasonable aspect ratio for the viewport
  float aspectRatio = 4.0f / 3.0f; // Default aspect ratio
  return glm::perspective(glm::radians(zoom), aspectRatio, nearPlane, farPlane);
}

void Camera3D::updateCameraVectors() {
  glm::vec3 newFront;
  newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
  newFront.y = sin(glm::radians(pitch));
  newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
  front = glm::normalize(newFront);

  right = glm::normalize(glm::cross(front, worldUp));
  up = glm::normalize(glm::cross(right, front));
}

// Renderer3D implementation
Renderer3D::Renderer3D()
    : initialized(false), viewportX(0), viewportY(0), viewportWidth(800), viewportHeight(600),
      lightPos(1.2f, 1.0f, 2.0f), lightColor(1.0f, 1.0f, 1.0f), ambientColor(0.2f, 0.2f, 0.2f),
      framebufferInitialized(false) {}

Renderer3D::~Renderer3D() {
  cleanup();
}

bool Renderer3D::initialize() {
  if (!setupOpenGL()) {
    return false;
  }

  setupLighting();
  initialized = true;
  return true;
}

bool Renderer3D::setupOpenGL() {
  // Enable depth testing
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);

  // Disable backface culling for debugging
  // glEnable(GL_CULL_FACE);
  // glCullFace(GL_BACK);

  return true;
}

void Renderer3D::setupLighting() {
  // Lighting is handled in shaders
}

void Renderer3D::cleanup() {
  // Cleanup is handled by individual objects
}

void Renderer3D::beginFrame() {
  glClearColor(1.0f, 0.0f, 1.0f, 1.0f); // Magenta for debugging
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
}

void Renderer3D::endFrame() {
  // Frame end processing if needed
}

void Renderer3D::renderModel(const Model3D& model) {
  if (!model.isLoaded())
    return;

  // Set up matrices
  glm::mat4 modelMatrix = glm::mat4(1.0f);

  // Center the model at origin and scale appropriately
  glm::vec3 modelCenter = model.getCenter();
  glm::vec3 modelSize = model.getSize();
  float maxSize = std::max({modelSize.x, modelSize.y, modelSize.z});
  float scale = 2.0f / maxSize; // Scale to fit in a 2x2x2 cube

  modelMatrix = glm::translate(modelMatrix, -modelCenter);               // Center at origin
  modelMatrix = glm::scale(modelMatrix, glm::vec3(scale, scale, scale)); // Scale to reasonable size

  glm::mat4 viewMatrix = camera.getViewMatrix();

  // Use actual viewport dimensions for projection matrix
  float aspectRatio = (float)viewportWidth / (float)viewportHeight;
  glm::mat4 projectionMatrix =
      glm::perspective(glm::radians(camera.zoom), aspectRatio, camera.nearPlane, camera.farPlane);

  // Debug: Print model bounds and camera info
  static bool debug_printed = false;
  if (!debug_printed) {
    std::cout << "=== 3D RENDERING DEBUG INFO ===" << std::endl;
    std::cout << "Camera position: (" << camera.position.x << ", " << camera.position.y << ", " << camera.position.z
              << ")" << std::endl;
    std::cout << "Camera front: (" << camera.front.x << ", " << camera.front.y << ", " << camera.front.z << ")"
              << std::endl;
    std::cout << "Camera zoom/FOV: " << camera.zoom << " degrees" << std::endl;
    std::cout << "Viewport: " << viewportWidth << "x" << viewportHeight << std::endl;
    std::cout << "Aspect ratio: " << aspectRatio << std::endl;
    std::cout << "Near plane: " << camera.nearPlane << ", Far plane: " << camera.farPlane << std::endl;

    // Calculate and print model bounds
    std::cout << "Model info:" << std::endl;
    std::cout << "  - Vertices: " << model.getVertexCount() << std::endl;
    std::cout << "  - Triangles: " << model.getTriangleCount() << std::endl;
    std::cout << "  - Meshes: " << model.getMeshCount() << std::endl;
    std::cout << "  - Bounds: (" << model.getMinBounds().x << ", " << model.getMinBounds().y << ", "
              << model.getMinBounds().z << ") to (" << model.getMaxBounds().x << ", " << model.getMaxBounds().y << ", "
              << model.getMaxBounds().z << ")" << std::endl;
    std::cout << "  - Center: (" << model.getCenter().x << ", " << model.getCenter().y << ", " << model.getCenter().z
              << ")" << std::endl;
    std::cout << "  - Size: (" << model.getSize().x << ", " << model.getSize().y << ", " << model.getSize().z << ")"
              << std::endl;

    // Show transformation info
    glm::vec3 modelCenter = model.getCenter();
    glm::vec3 modelSize = model.getSize();
    float maxSize = std::max({modelSize.x, modelSize.y, modelSize.z});
    float scale = 2.0f / maxSize;

    std::cout << "Transformation:" << std::endl;
    std::cout << "  - Model center: (" << modelCenter.x << ", " << modelCenter.y << ", " << modelCenter.z << ")"
              << std::endl;
    std::cout << "  - Max size: " << maxSize << std::endl;
    std::cout << "  - Scale factor: " << scale << std::endl;
    std::cout << "  - Translation: (" << -modelCenter.x << ", " << -modelCenter.y << ", " << -modelCenter.z << ")"
              << std::endl;

    debug_printed = true;
  }

  // Use the model's shader program
  unsigned int shaderProgram = model.getShaderProgram();
  if (shaderProgram == 0) {
    std::cerr << "Error: Shader program is 0!" << std::endl;
    return;
  }

  glUseProgram(shaderProgram);

  // Set uniforms
  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(modelMatrix));
  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(viewMatrix));
  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projectionMatrix));

  glUniform3fv(glGetUniformLocation(shaderProgram, "lightPos"), 1, glm::value_ptr(lightPos));
  glUniform3fv(glGetUniformLocation(shaderProgram, "lightColor"), 1, glm::value_ptr(lightColor));
  glUniform3fv(glGetUniformLocation(shaderProgram, "ambientColor"), 1, glm::value_ptr(ambientColor));
  glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"), 1, glm::value_ptr(camera.position));

  // Check for OpenGL errors
  GLenum err;
  while ((err = glGetError()) != GL_NO_ERROR) {
    std::cerr << "OpenGL error before render: " << err << std::endl;
  }

  // Render the model
  model.render();

  // Check for OpenGL errors after render
  while ((err = glGetError()) != GL_NO_ERROR) {
    std::cerr << "OpenGL error after render: " << err << std::endl;
  }
}

// Add a simple test triangle renderer for debugging
void Renderer3D::renderTestTriangle() {
  // Simple colored triangle for testing
  static unsigned int testVAO = 0, testVBO = 0;
  static bool testInitialized = false;

  if (!testInitialized) {
    float vertices[] = {
        -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, // Red
        0.5f,  -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, // Green
        0.0f,  0.5f,  0.0f, 0.0f, 0.0f, 1.0f  // Blue
    };

    glGenVertexArrays(1, &testVAO);
    glGenBuffers(1, &testVBO);

    glBindVertexArray(testVAO);
    glBindBuffer(GL_ARRAY_BUFFER, testVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    testInitialized = true;
  }

  // Use a simple shader for the test triangle
  static unsigned int testShader = 0;
  if (testShader == 0) {
    const char* vertexShaderSource = R"(
      #version 330 core
      layout (location = 0) in vec3 aPos;
      layout (location = 1) in vec3 aColor;
      out vec3 ourColor;
      uniform mat4 projection;
      uniform mat4 view;
      uniform mat4 model;
      void main() {
        gl_Position = projection * view * model * vec4(aPos, 1.0);
        ourColor = aColor;
      }
    )";

    const char* fragmentShaderSource = R"(
      #version 330 core
      out vec4 FragColor;
      in vec3 ourColor;
      void main() {
        FragColor = vec4(ourColor, 1.0);
      }
    )";

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    testShader = glCreateProgram();
    glAttachShader(testShader, vertexShader);
    glAttachShader(testShader, fragmentShader);
    glLinkProgram(testShader);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
  }

  glUseProgram(testShader);

  glm::mat4 modelMatrix = glm::mat4(1.0f);
  glm::mat4 viewMatrix = camera.getViewMatrix();

  // Use actual viewport dimensions for projection matrix
  float aspectRatio = (float)viewportWidth / (float)viewportHeight;
  glm::mat4 projectionMatrix =
      glm::perspective(glm::radians(camera.zoom), aspectRatio, camera.nearPlane, camera.farPlane);

  glUniformMatrix4fv(glGetUniformLocation(testShader, "model"), 1, GL_FALSE, glm::value_ptr(modelMatrix));
  glUniformMatrix4fv(glGetUniformLocation(testShader, "view"), 1, GL_FALSE, glm::value_ptr(viewMatrix));
  glUniformMatrix4fv(glGetUniformLocation(testShader, "projection"), 1, GL_FALSE, glm::value_ptr(projectionMatrix));

  glBindVertexArray(testVAO);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
}

void Renderer3D::setViewport(int x, int y, int width, int height) {
  viewportX = x;
  viewportY = y;
  viewportWidth = width;
  viewportHeight = height;
}

void Renderer3D::setClearColor(float r, float g, float b, float a) {
  glClearColor(r, g, b, a);
}

unsigned int Renderer3D::renderToTexture(int width, int height, const Model3D& model) {
  // Initialize framebuffer if not already done or if size changed
  if (!framebufferInitialized || width != viewportWidth || height != viewportHeight) {
    if (framebufferInitialized) {
      cleanupTexture(renderTexture);
    }

    viewportWidth = width;
    viewportHeight = height;

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    // Create color texture
    glGenTextures(1, &renderTexture);
    glBindTexture(GL_TEXTURE_2D, renderTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTexture, 0);

    // Create depth texture
    glGenTextures(1, &depthTexture);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      std::cerr << "Framebuffer incomplete: 0x" << std::hex << status << std::endl;
      return 0;
    } else {
      std::cout << "Framebuffer complete. Color tex: " << renderTexture << ", Depth tex: " << depthTexture << std::endl;
    }
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
      std::cerr << "OpenGL error after framebuffer setup: 0x" << std::hex << err << std::endl;
    }
    framebufferInitialized = true;
  }

  // Bind framebuffer and render
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glViewport(0, 0, width, height);

  // Clear and render
  glClearColor(0.2f, 0.3f, 0.3f, 1.0f); // Dark teal background
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Always render test triangle for now
  renderTestTriangle();

  GLenum err = glGetError();
  if (err != GL_NO_ERROR) {
    std::cerr << "OpenGL error after rendering: 0x" << std::hex << err << std::endl;
  }
  // Unbind framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  return renderTexture;
}

void Renderer3D::cleanupTexture(unsigned int textureId) {
  if (textureId == renderTexture) {
    glDeleteTextures(1, &renderTexture);
    glDeleteTextures(1, &depthTexture);
    glDeleteFramebuffers(1, &framebuffer);
    framebufferInitialized = false;
  }
}

// Utility functions
namespace ModelUtils {
std::string getModelInfo(const Model3D& model) {
  if (!model.isLoaded()) {
    return "No model loaded";
  }

  std::string info = "File: " + model.getFilepath() + "\n";
  info += "Meshes: " + std::to_string(model.getMeshCount()) + "\n";
  info += "Vertices: " + std::to_string(model.getVertexCount()) + "\n";
  info += "Triangles: " + std::to_string(model.getTriangleCount());

  return info;
}

bool isModelFile(const std::string& filepath) {
  std::filesystem::path path(filepath);
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

  return extension == ".obj" || extension == ".fbx" || extension == ".dae" || extension == ".3ds" ||
         extension == ".blend" || extension == ".max" || extension == ".c4d" || extension == ".lwo" ||
         extension == ".lws" || extension == ".md2" || extension == ".md3" || extension == ".md5mesh" ||
         extension == ".b3d" || extension == ".bvh" || extension == ".ply" || extension == ".stl" ||
         extension == ".x" || extension == ".gltf" || extension == ".glb";
}

std::string getModelFormat(const std::string& filepath) {
  std::filesystem::path path(filepath);
  return path.extension().string();
}
} // namespace ModelUtils
