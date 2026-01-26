R"(#version 330 core
out vec4 FragColor;

in vec4 v_color;
in vec2 v_texCoords;

uniform sampler2D u_texture;

void main()
{
    FragColor = v_color * texture2D(u_texture, v_texCoords);
}
)"
