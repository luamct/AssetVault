#pragma once

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
struct aiMesh;
struct aiMaterial;

class Model3D {
public:
  struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
  };

  struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    unsigned int vao, vbo, ebo;
    bool hasNormals;
    bool hasTexCoords;
  };

  Model3D();
  ~Model3D();

  bool load(const std::string& filepath);
  void render() const;
  void cleanup();

  // Getters for model info
  const std::string& getFilepath() const {
    return filepath;
  }
  bool isLoaded() const {
    return loaded;
  }
  size_t getMeshCount() const {
    return meshes.size();
  }
  size_t getVertexCount() const {
    return totalVertices;
  }
  size_t getTriangleCount() const {
    return totalTriangles;
  }
  unsigned int getShaderProgram() const {
    return shaderProgram;
  }

  // Model bounds
  glm::vec3 getMinBounds() const {
    return minBounds;
  }
  glm::vec3 getMaxBounds() const {
    return maxBounds;
  }
  glm::vec3 getCenter() const {
    return (minBounds + maxBounds) * 0.5f;
  }
  glm::vec3 getSize() const {
    return maxBounds - minBounds;
  }

private:
  void processNode(aiNode* node, const aiScene* scene);
  void processMesh(aiMesh* mesh, const aiScene* scene);
  void setupMesh(Mesh& mesh);
  void setupShaders();

  std::string filepath;
  bool loaded;
  std::vector<Mesh> meshes;
  size_t totalVertices;
  size_t totalTriangles;

  // Model bounds
  glm::vec3 minBounds;
  glm::vec3 maxBounds;

  // OpenGL shader program
  unsigned int shaderProgram;
  unsigned int vertexShader;
  unsigned int fragmentShader;
};

class Camera3D {
public:
  Camera3D();

  void update(float deltaTime);
  void handleMouse(float xoffset, float yoffset, bool constrainPitch = true);
  void handleScroll(float yoffset);
  void updateCameraVectors();

  glm::mat4 getViewMatrix() const;
  glm::mat4 getProjectionMatrix() const;

  // Camera properties
  glm::vec3 position;
  glm::vec3 front;
  glm::vec3 up;
  glm::vec3 right;
  glm::vec3 worldUp;

  // Euler angles
  float yaw;
  float pitch;

  // Camera options
  float movementSpeed;
  float mouseSensitivity;
  float zoom;
  float fov;
  float nearPlane;
  float farPlane;

  // Mouse state
  bool firstMouse;
  float lastX;
  float lastY;
};

class Renderer3D {
public:
  Renderer3D();
  ~Renderer3D();

  bool initialize();
  void cleanup();
  void beginFrame();
  void endFrame();
  void renderModel(const Model3D& model);

  void setViewport(int x, int y, int width, int height);
  void setClearColor(float r, float g, float b, float a = 1.0f);
  void renderTestTriangle(); // For debugging

  // Render to texture for ImGui integration
  unsigned int renderToTexture(int width, int height, const Model3D& model);
  void cleanupTexture(unsigned int textureId);

  Camera3D& getCamera() {
    return camera;
  }

private:
  bool setupOpenGL();
  void setupLighting();

  Camera3D camera;
  bool initialized;

  // Viewport
  int viewportX, viewportY, viewportWidth, viewportHeight;

  // Lighting
  glm::vec3 lightPos;
  glm::vec3 lightColor;
  glm::vec3 ambientColor;

  // Framebuffer for texture rendering
  unsigned int framebuffer;
  unsigned int renderTexture;
  unsigned int depthTexture;
  bool framebufferInitialized;
};

// Utility functions
namespace ModelUtils {
std::string getModelInfo(const Model3D& model);
bool isModelFile(const std::string& filepath);
std::string getModelFormat(const std::string& filepath);
} // namespace ModelUtils
