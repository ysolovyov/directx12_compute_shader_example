RWBuffer<int> srcBuffer: register(u0);
RWBuffer<int> dstBuffer: register(u1);

[numthreads(1, 1, 1)]
void main(uint3 groupID : SV_GroupID, uint3 tid : SV_DispatchThreadID, uint3 localTID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    const int index = tid.x;
    dstBuffer[index] = 99;//srcBuffer[index] + 10;
}
