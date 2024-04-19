#version 330 core
out uvec4 FragColor;

in vec4 v_color;
in vec2 v_texCoords;

uniform sampler2D u_texture;
uniform int u_slotIndex;
uniform int u_width;
uniform int u_height;
uniform bool u_OnlyUndrawn;

void main()
{
    float red = texture(u_texture, v_texCoords).r;
    // 为零时透明的
    if (red < 0.05)
    {
		discard;
	}
    // 不为1时表明被绘制过
    if ((u_OnlyUndrawn && red < 0.95) || u_slotIndex == 65535) {
        FragColor = uvec4(0, 0, 0, 1);
		return;
    }

 //   float texX = v_texCoords.x * u_width;
 //   uint texXInt = uint(texX);
 //   float texY = v_texCoords.y * u_height;
 //   uint texYInt = uint(texY);

 //   float texXFraction = texX - float(texXInt);
 //   float texYFraction = texY - float(texYInt);

 //   if (abs(texXFraction - 0.5) > 0.4 || abs(texYFraction - 0.5) > 0.4)
 //   {
	//	FragColor = uvec4(0, 0, 0, 1);
	//	return;
	//}

    uint index = uint(v_texCoords.y * u_height) * uint(u_width) + uint(v_texCoords.x * u_width);
    uint Slot_V_U = (uint(u_slotIndex) << 24) | index;
    FragColor = uvec4(Slot_V_U, 0, 0, 1);
}