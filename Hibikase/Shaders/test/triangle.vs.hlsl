struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    static const float2 positions[3] =
    {
        float2(0.0f, 0.65f),
        float2(0.65f, -0.65f),
        float2(-0.65f, -0.65f)
    };

    static const float3 colors[3] =
    {
        float3(1.0f, 0.2f, 0.2f),
        float3(0.2f, 1.0f, 0.3f),
        float3(0.2f, 0.5f, 1.0f)
    };

    VSOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.color = colors[vertexId];
    return output;
}
