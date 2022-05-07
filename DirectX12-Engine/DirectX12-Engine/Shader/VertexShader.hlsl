struct VSOut
{
	float4 color : Color;
	float4 position : SV_Position;
};

VSOut main(float3 pos : POSITION, float4 color : Color)
{
	VSOut vso;
	vso.position = float4(pos, 1.0f);
	vso.color = color;
	return vso;
}