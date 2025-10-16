#version 330 core
in vec3 FragPos;
flat in vec3 Normal;  // Flat shading - no interpolation
in vec2 TexCoord;

uniform vec3 lightDir;  // Directional light direction
uniform vec3 lightColor;
uniform vec3 materialColor;
uniform vec3 emissiveColor;

out vec4 FragColor;

void main()
{
    vec3 norm = normalize(Normal);

    // Ambient lighting
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * lightColor;

    // Directional diffuse lighting (uniform across surfaces with same normal)
    float diff = max(dot(norm, -lightDir), 0.0);
    vec3 diffuse = diff * lightColor * 0.7;

    vec3 result = (ambient + diffuse) * materialColor + emissiveColor;
    FragColor = vec4(result, 1.0);
}
