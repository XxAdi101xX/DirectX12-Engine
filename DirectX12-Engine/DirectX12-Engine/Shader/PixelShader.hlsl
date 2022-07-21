cbuffer ConstantBuffer : register(b0)
{
	float cb[64];
};

// u0 is the render target
RWTexture2D<float> myTexture : register(u1);

float4 main(float4 color : Color, float4 position : SV_Position) : SV_TARGET
{
	//color.x = cb[0];
	//color.y = cb[0];
	//color.z = cb[0];
	//color.x = myTexture[uint2(0, 0)];
	//color.y = myTexture[uint2(0, 0)];
	//color.z = myTexture[uint2(0, 0)];
	return color;
}