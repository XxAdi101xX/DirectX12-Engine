cbuffer DataBuffer : register(b0)
{
	float cb[64];
};

float4 main(float4 color : Color, float4 position : SV_Position) : SV_TARGET
{
	color.x = cb[0];
	return color;
}