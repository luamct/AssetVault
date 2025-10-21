#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aBoneIds;
layout (location = 4) in vec4 aBoneWeights;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform bool enableSkinning;
uniform int boneCount;

const int MAX_BONES = 128;
uniform mat4 boneMatrices[MAX_BONES];

void main()
{
    mat3 modelNormalMatrix = mat3(transpose(inverse(model)));
    vec4 modelPosition = model * vec4(aPos, 1.0);
    vec3 baseNormal = modelNormalMatrix * aNormal;

    vec4 worldPosition = modelPosition;
    vec3 worldNormal = baseNormal;

    if (enableSkinning && boneCount > 0) {
        mat4 skinMatrix = mat4(0.0);
        vec3 skinnedNormal = vec3(0.0);

        for (int i = 0; i < 4; ++i) {
            float weight = aBoneWeights[i];
            if (weight <= 0.0) {
                continue;
            }

            int boneIndex = int(aBoneIds[i]);
            if (boneIndex < 0 || boneIndex >= boneCount) {
                continue;
            }

            mat4 boneTransform = boneMatrices[boneIndex];
            skinMatrix += boneTransform * weight;
            skinnedNormal += (mat3(boneTransform) * aNormal) * weight;
        }

        worldPosition = skinMatrix * vec4(aPos, 1.0);
        if (length(skinnedNormal) > 0.0) {
            worldNormal = normalize(skinnedNormal);
        }
        else {
            worldNormal = normalize(baseNormal);
        }
    }
    else {
        worldNormal = normalize(baseNormal);
    }

    FragPos = worldPosition.xyz;
    Normal = worldNormal;
    TexCoord = aTexCoord;
    gl_Position = projection * view * worldPosition;
}
