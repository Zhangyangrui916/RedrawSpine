R"(#version 330 core
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
    
    // 假设底层是另一个alpha = 255的Attachment,那么我们应该用alpha=128作为上层Attachment与下层Attachment的边界
    // 假设底层是背景,那么我们应该用alpha>0作为上层Attachment与背景的边界 (那么上层会比较胖)
    // 折中在两者之间选一个阈值
    if (red < 0.2)
    {
        discard;
    }
    if (u_slotIndex == 65535) {
        FragColor = uvec4(0, 0, 0, 1);
        return;
    }

    FragColor = uvec4(u_slotIndex * 3 % 256, 0, 0, 1);
}
)"
