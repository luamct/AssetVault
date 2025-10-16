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
uniform vec3 emissiveColor;

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

    vec3 result = (ambient + diffuse + fillLight + specular + rimLight) * objectColor + emissiveColor;
    FragColor = vec4(result, 1.0);
}
