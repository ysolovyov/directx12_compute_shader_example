#include "pch.h"

using Microsoft::WRL::ComPtr;

const char* shaderSource = R"(
    // Input buffers
    Buffer<float> A : register(t0);  // Input buffer A
    Buffer<float> B : register(t1);  // Input buffer B

    // Output buffer
    RWBuffer<float> output : register(u0); // Output UAV buffer

    [numthreads(1, 1, 1)] // Number of threads per thread group
    void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
    {
        uint idx = dispatchThreadID.x;  // Get the index of the element
        output[idx] = A[idx] + B[idx]; // Add corresponding elements
    }
)";

const size_t numElements = 1024;     // Number of elements in the arrays
const size_t bufferSize = numElements * sizeof(float);  // Size of the buffers (in bytes)

// Global DirectX 12 objects
ComPtr<ID3D12Device> device;
ComPtr<ID3D12CommandQueue> commandQueue;
ComPtr<ID3D12CommandAllocator> commandAllocator;
ComPtr<ID3D12GraphicsCommandList> commandList;
ComPtr<ID3D12RootSignature> rootSignature;
ComPtr<ID3D12PipelineState> computePSO;

// Buffers
ComPtr<ID3D12Resource> ABuffer;
ComPtr<ID3D12Resource> BBuffer;
ComPtr<ID3D12Resource> outputBuffer;
ComPtr<ID3D12Resource> stagingBuffer;

// Function declarations
void InitializeDirectX();
void CreateBuffers();
ComPtr<ID3DBlob> CompileShader();
void CreateRootSignature();
void CreatePipelineState(ComPtr<ID3DBlob> shaderBlob);
void DispatchComputeShader();
void ReadResults();
void Cleanup();

void InitializeDirectX()
{
    HRESULT hr;

    // Create the device
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    assert(SUCCEEDED(hr));

    // Create a command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    assert(SUCCEEDED(hr));

    // Create a command allocator
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&commandAllocator));
    assert(SUCCEEDED(hr));
}

void CreateBuffers()
{
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = bufferSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    // Create buffer A (input buffer)
    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, // Initial state
        nullptr,
        IID_PPV_ARGS(&ABuffer)
    );
    assert(SUCCEEDED(hr));

    // Create buffer B (input buffer)
    hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, // Initial state
        nullptr,
        IID_PPV_ARGS(&BBuffer)
    );
    assert(SUCCEEDED(hr));

    // Create output buffer (UAV)
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; // Allow UAV usage
    hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, // Initial state for UAV
        nullptr,
        IID_PPV_ARGS(&outputBuffer)
    );
    assert(SUCCEEDED(hr));
}

ComPtr<ID3DBlob> CompileShader()
{
    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    // HRESULT hr = D3DCompileFromFile(L"Shader.hlsl", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &shaderBlob, &errorBlob);
    HRESULT hr = D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &shaderBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            std::cerr << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        assert(false && "Shader compilation failed");
    }
    return shaderBlob;
}

void CreateRootSignature()
{
    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 0;
    rootSigDesc.NumStaticSamplers = 0;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rootSignatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D12_ROOT_SIGNATURE_VERSION_1, &rootSignatureBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            std::cerr << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        assert(false && "Root signature creation failed");
    }

    hr = device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
}

void CreatePipelineState(ComPtr<ID3DBlob> shaderBlob)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };

    HRESULT hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&computePSO));
    assert(SUCCEEDED(hr));
}

void DispatchComputeShader()
{
    HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
    assert(SUCCEEDED(hr));

    // Set pipeline state (compute shader)
    commandList->SetPipelineState(computePSO.Get());

    // Create UAV for the output buffer
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ComPtr<ID3D12DescriptorHeap> uavHeap;
    hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&uavHeap));
    assert(SUCCEEDED(hr));

    // Create UAV for the output buffer
    D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = uavHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateUnorderedAccessView(outputBuffer.Get(), nullptr, nullptr, uavHandle);

    // Bind UAV root signature
    commandList->SetComputeRootDescriptorTable(0, uavHeap->GetGPUDescriptorHandleForHeapStart());

    // Dispatch compute shader (1 thread group per element)
    commandList->Dispatch(numElements / 64, 1, 1);

    // Execute the command list
    commandList->Close();
    ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}

void ReadResults()
{
    D3D12_RESOURCE_DESC stagingBufferDesc = {};
    stagingBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    stagingBufferDesc.Width = bufferSize;
    stagingBufferDesc.Height = 1;
    stagingBufferDesc.DepthOrArraySize = 1;
    stagingBufferDesc.MipLevels = 1;
    stagingBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    stagingBufferDesc.SampleDesc.Count = 1;
    stagingBufferDesc.SampleDesc.Quality = 0;
    stagingBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES heapPropsCPU = {};
    heapPropsCPU.Type = D3D12_HEAP_TYPE_READBACK;

    HRESULT hr = device->CreateCommittedResource(
        &heapPropsCPU,
        D3D12_HEAP_FLAG_NONE,
        &stagingBufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,  // Initial state for copying
        nullptr,
        IID_PPV_ARGS(&stagingBuffer)
    );
    assert(SUCCEEDED(hr));

    // Copy the result to the staging buffer
    commandList->CopyBufferRegion(stagingBuffer.Get(), 0, outputBuffer.Get(), 0, bufferSize);

    // Transition staging buffer for reading
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = stagingBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList->ResourceBarrier(1, &barrier);

    // Execute the command list
    commandList->Close();
    commandQueue->ExecuteCommandLists(1, &commandList);

    // Map the staging buffer and read the data
    float* data = nullptr;
    stagingBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
    for (size_t i = 0; i < numElements; ++i)
    {
        std::cout << "Result[" << i << "] = " << data[i] << std::endl;
    }
    stagingBuffer->Unmap(0, nullptr);
}

void Cleanup()
{
    ABuffer.Reset();
    BBuffer.Reset();
    outputBuffer.Reset();
    stagingBuffer.Reset();
    commandList.Reset();
    commandAllocator.Reset();
    commandQueue.Reset();
    rootSignature.Reset();
    computePSO.Reset();
    device.Reset();
}

int main()
{
    InitializeDirectX();
    ComPtr<ID3DBlob> shaderBlob = CompileShader();
    CreateRootSignature();
    CreateBuffers();
    CreatePipelineState(shaderBlob);

    // Create command allocator and list
    HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&commandAllocator));
    assert(SUCCEEDED(hr));

    // Dispatch compute shader
    DispatchComputeShader();

    // Read back results
    ReadResults();

    // Clean up
    Cleanup();

    return 0;
}
