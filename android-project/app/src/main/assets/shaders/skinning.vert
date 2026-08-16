// The #version and precision qualifiers are injected by createShaderProgram

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aJoints; // Up to 4 influencing joints per vertex
layout (location = 3) in vec4 aWeights; // Weights corresponding to each joint (must sum to 1.0)

out vec2 vTexCoord;

uniform mat4 uMvpMatrix;

// Maximum number of joints supported in the skinning palette uniform array
const int MAX_JOINTS = 100;
uniform mat4 uJointMatrices[MAX_JOINTS];

void main()
{
    // Calculate the skinned position by blending the weighted joint transformations
    mat4 skinMatrix = 
          aWeights.x * uJointMatrices[int(aJoints.x)]
        + aWeights.y * uJointMatrices[int(aJoints.y)]
        + aWeights.z * uJointMatrices[int(aJoints.z)]
        + aWeights.w * uJointMatrices[int(aJoints.w)];

    vec4 skinnedPos = skinMatrix * vec4(aPosition, 1.0);

    vTexCoord = aTexCoord;
    gl_Position = uMvpMatrix * skinnedPos;
}
