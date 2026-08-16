// The #version and precision qualifiers are injected by createShaderProgram

out vec4 fragColor;

uniform vec4 uColor;

void main()
{
    fragColor = uColor;
}
