#version 330 core

in vec4 v_Color;
in vec2 v_TexCoord;
flat in float v_TexIndex;
out vec4 FragColor;

uniform sampler2D u_Textures[16];

void main()
{
    int index = int(v_TexIndex);
    FragColor = texture(u_Textures[index], v_TexCoord) * v_Color;
}