#version 330 core
in vec3 FragPos;
flat in vec3 Normal;  // Flat shading - receives non-interpolated normal from vertex shader
in vec2 TexCoord;

// Lighting uniforms
uniform vec3 lightPos;        // Point light position (typically camera position)
uniform vec3 viewPos;         // Camera/view position
uniform vec3 lightColor;

// Material uniforms
uniform sampler2D diffuseTexture;
uniform bool useTexture;
uniform vec3 materialColor;
uniform vec3 emissiveColor;

// Lighting intensity controls (0.0 = disabled, 1.0 = full strength)
uniform float ambientIntensity;
uniform float diffuseIntensity;
uniform float fillLightIntensity;
uniform float specularIntensity;
uniform float rimLightIntensity;

out vec4 FragColor;

void main()
{
    // Sample texture color or use material color
    vec3 objectColor = useTexture ? texture(diffuseTexture, TexCoord).rgb : materialColor;

    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);

    // Ambient lighting (controlled by intensity)
    vec3 ambient = ambientIntensity * 0.25 * lightColor;

    // Main diffuse light (point light from lightPos)
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diffuseIntensity * diff * lightColor * 0.7;

    // Fill light from opposite direction (controlled by intensity)
    vec3 fillLightDir = normalize(-lightPos);
    float fillDiff = max(dot(norm, fillLightDir), 0.0);
    vec3 fillLight = fillLightIntensity * fillDiff * lightColor * 0.15;

    // Specular highlights (controlled by intensity)
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 64);
    vec3 specular = specularIntensity * 0.2 * spec * lightColor;

    // Rim lighting for shape definition (controlled by intensity)
    float rimFactor = 1.0 - max(dot(viewDir, norm), 0.0);
    vec3 rimLight = rimLightIntensity * 0.3 * pow(rimFactor, 3.0) * lightColor;

    vec3 result = (ambient + diffuse + fillLight + specular + rimLight) * objectColor + emissiveColor;
    FragColor = vec4(result, 1.0);
}
