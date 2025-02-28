#include "pch.h"

using Microsoft::WRL::ComPtr;

const char* shaderSource = R"(
    RWBuffer<float> src : register(u0);
    RWBuffer<float> dst : register(u1);

    [numthreads(32, 1, 1)]
    void main(uint3 blockID : SV_GroupID, uint3 threadID : SV_GroupThreadID) {
        uint x = blockID.x * 32 + threadID.x;
        dst[x] = sqrt(src[x]);
    }
)";

static UINT DivUp(UINT a, UINT b)
{
    return (a + b - 1) / b;
}

int main()
{
    const UINT numElements = 35;

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;

    UINT64 fenceValue = 0;
    ComPtr<ID3D12Fence> fence;
    HANDLE eventHandle;

    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> computePSO;
    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3D12DescriptorHeap> uavHeap;
    ComPtr<ID3D12Resource> uploadBuffer;
    ComPtr<ID3D12Resource> stagingBuffer;
    ComPtr<ID3D12Resource> uavBufferSrc;
    ComPtr<ID3D12Resource> uavBufferDst;

    // Create the device
    THROW_IF_FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));

    // Create a command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    THROW_IF_FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

    // Create command list
    THROW_IF_FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&commandAllocator)));
    THROW_IF_FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

    // Create fence
    THROW_IF_FAILED(device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // Create UAV buffers
    D3D12_HEAP_PROPERTIES uavHeapProps = {};
    uavHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    uavHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uavHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC uavBufferDesc = {};
    uavBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uavBufferDesc.Width = numElements * sizeof(float); // Total size of the buffer
    uavBufferDesc.Height = 1;
    uavBufferDesc.DepthOrArraySize = 1;
    uavBufferDesc.MipLevels = 1;
    uavBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavBufferDesc.SampleDesc.Count = 1;
    uavBufferDesc.SampleDesc.Quality = 0;
    uavBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    uavBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    THROW_IF_FAILED(device->CreateCommittedResource(
        &uavHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uavBufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr, 
        IID_PPV_ARGS(&uavBufferSrc)));

    THROW_IF_FAILED(device->CreateCommittedResource(
        &uavHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uavBufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&uavBufferDst)));

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = numElements * sizeof(float); // Total size of the buffer
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD; // This is a upload buffer.
    uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    
    // Create upload buffer
    THROW_IF_FAILED(device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, // Initial state for uploading
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    // Map the upload buffer and write the data
    float* src = nullptr;
    uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&src));
    for (size_t i = 0; i < numElements; ++i) {
        src[i] = (float)i;
    }
    uploadBuffer->Unmap(0, nullptr);

    D3D12_HEAP_PROPERTIES stagingHeapProps = {};
    stagingHeapProps.Type = D3D12_HEAP_TYPE_READBACK; // This is a readback buffer.
    stagingHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    stagingHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    // Create staging buffer
    THROW_IF_FAILED(device->CreateCommittedResource(
        &stagingHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, // Initial state for copying
        nullptr,
        IID_PPV_ARGS(&stagingBuffer)));

    D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {};
    uavHeapDesc.NumDescriptors = 2;
    uavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    uavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // Must be visible to shaders

    THROW_IF_FAILED(device->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&uavHeap)));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN; // Buffer format
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = numElements;
    uavDesc.Buffer.StructureByteStride = sizeof(float);
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    UINT uavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(uavHeap->GetCPUDescriptorHandleForHeapStart());
    device->CreateUnorderedAccessView(uavBufferSrc.Get(), nullptr, &uavDesc, uavHandle);
    uavHandle.ptr += uavDescriptorSize;
    device->CreateUnorderedAccessView(uavBufferDst.Get(), nullptr, &uavDesc, uavHandle);

    THROW_IF_FAILED(D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &shaderBlob, nullptr));

    D3D12_ROOT_PARAMETER rootParams[1] = {};

    D3D12_DESCRIPTOR_RANGE descRange = {};
    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descRange.NumDescriptors = 2;
    descRange.BaseShaderRegister = 0; // u0

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &descRange;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rootSignatureBlob;
    THROW_IF_FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSignatureBlob, nullptr));
    THROW_IF_FAILED(device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };

    THROW_IF_FAILED(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&computePSO)));

    ID3D12DescriptorHeap* heaps[] = { uavHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetPipelineState(computePSO.Get());
    commandList->SetComputeRootSignature(rootSignature.Get());
    commandList->SetComputeRootDescriptorTable(0, uavHeap->GetGPUDescriptorHandleForHeapStart());

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(uavBufferSrc.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));
    commandList->CopyResource(uavBufferSrc.Get(), uploadBuffer.Get());
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(uavBufferSrc.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    commandList->Dispatch(DivUp(numElements, 32), 1, 1);

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(uavBufferDst.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));
    commandList->CopyResource(stagingBuffer.Get(), uavBufferDst.Get());
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(uavBufferDst.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    commandList->Close();

    ID3D12CommandList* commandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(ARRAYSIZE(commandLists), commandLists);

    commandQueue->Signal(fence.Get(), ++fenceValue);

    // Wait for the fence to reach the signal value before proceeding
    fence->SetEventOnCompletion(fenceValue, eventHandle);
    WaitForSingleObject(eventHandle, INFINITE);

    // Map the staging buffer and read the data
    float* dst = nullptr;
    stagingBuffer->Map(0, nullptr, reinterpret_cast<void**>(&dst));
    for (size_t i = 0; i < numElements; ++i) {
        std::cout << "dst[" << i << "] = " << dst[i] << std::endl;
    }
    stagingBuffer->Unmap(0, nullptr);

    CloseHandle(eventHandle);

    return 0;
}
