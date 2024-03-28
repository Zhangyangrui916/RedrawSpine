#version 330 core
layout (location = 0) in vec2 xy;
layout (location = 1) in vec4 color;
layout (location = 2) in vec2 uv;

out vec4 v_color;
out vec2 v_texCoords;

void main()
{
    v_color = color;
    v_texCoords = uv;
    gl_Position = vec4(xy.x / 60.f, xy.y/ 60.f, 0.0, 1.0);
}