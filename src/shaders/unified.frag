#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

// Lighting uniforms
uniform vec3 lightDir;        // Directional light direction
uniform vec3 lightColor;

// Material uniforms
uniform sampler2D diffuseTexture;
uniform bool useTexture;
uniform vec3 materialColor;
uniform vec3 emissiveColor;

// Lighting intensity controls (0.0 = disabled, 1.0 = full strength)
uniform float ambientIntensity;
uniform float diffuseIntensity;

out vec4 FragColor;

void main()
{
    // Sample texture color or use material color
    vec3 objectColor = useTexture ? texture(diffuseTexture, TexCoord).rgb : materialColor;

    vec3 norm = normalize(Normal);

    // Ambient lighting (controlled by intensity)
    vec3 ambient = ambientIntensity * lightColor;

    // Directional diffuse lighting
    float diff = max(dot(norm, -lightDir), 0.0);
    vec3 diffuse = diffuseIntensity * diff * lightColor;

    vec3 result = (ambient + diffuse) * objectColor + emissiveColor;
    FragColor = vec4(result, 1.0);
}
