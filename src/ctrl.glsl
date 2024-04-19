#version 330 core
out uvec4 FragColor;

in vec4 v_color;
in vec2 v_texCoords;

uniform sampler2D u_texture;
uniform int u_slotIndex;
uniform int u_width;
uniform int u_height;

void main()
{
    float red = texture(u_texture, v_texCoords).r;
    // 为零时透明的
    if (red < 0.031)
    {
        discard;
    }
    if (u_slotIndex == 65535) {
        FragColor = uvec4(0, 0, 0, 1);
        return;
    }

    FragColor = uvec4(u_slotIndex * 3 % 256, 0, 0, 1);
}