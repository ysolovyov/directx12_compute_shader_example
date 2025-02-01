// Input buffers
Buffer<float> A : register(t0);  // Input buffer A
Buffer<float> B : register(t1);  // Input buffer B

// Output buffer
RWBuffer<float> output : register(u0); // Output UAV buffer

[numthreads(1, 1, 1)] // Number of threads per thread group
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint idx = dispatchThreadID.x;  // Get the index of the element
    output[idx] = A[idx] + B[idx]; // Add corresponding elements
}
