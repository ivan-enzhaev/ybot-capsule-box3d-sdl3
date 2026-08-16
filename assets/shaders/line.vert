// The #version is injected by createShaderProgram

layout (location = 0) in vec3 aPosition;

uniform mat4 uMvpMatrix;

void main()
{
    gl_Position = uMvpMatrix * vec4(aPosition, 1.0f);
}
