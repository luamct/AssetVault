#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform vec3 lightDir;
uniform vec3 lightColor;

uniform sampler2D diffuseTexture;
uniform vec3 emissiveColor;

uniform float ambientIntensity;
uniform float diffuseIntensity;

out vec4 FragColor;

const float GAMMA = 2.2;
const vec3 INV_GAMMA = vec3(1.0 / GAMMA);
const vec3 GAMMA_VEC = vec3(GAMMA);

vec3 srgb_to_linear(vec3 srgb_color) {
    vec3 safe = clamp(srgb_color, vec3(0.0), vec3(1.0));
    return pow(safe, GAMMA_VEC);
}

vec3 linear_to_srgb(vec3 linear_color) {
    vec3 safe = max(linear_color, vec3(0.0));
    return pow(safe, INV_GAMMA);
}

void main()
{
    vec3 objectColor = srgb_to_linear(texture(diffuseTexture, TexCoord).rgb);
    vec3 emissiveLinear = clamp(emissiveColor, vec3(0.0), vec3(1.0));
    vec3 norm = normalize(Normal);

    vec3 ambient = ambientIntensity * lightColor;
    float diff = max(dot(norm, -lightDir), 0.0);
    vec3 diffuse = diffuseIntensity * diff * lightColor;

    vec3 linearColor = (ambient + diffuse) * objectColor + emissiveLinear;
    linearColor = max(linearColor, vec3(0.0));
    vec3 srgbColor = linear_to_srgb(linearColor);
    FragColor = vec4(srgbColor, 1.0);
}
