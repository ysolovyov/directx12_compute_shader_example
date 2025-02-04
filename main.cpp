#include "pch.h"

using Microsoft::WRL::ComPtr;

const char* shaderSource = R"(
    RWBuffer<float> uavBuffer : register(u0);  // UAV (writeable buffer)

    [numthreads(1, 1, 1)]
    void main(uint3 DTid : SV_DispatchThreadID) {
        // Write some data to the buffer
        uavBuffer[DTid.x] = (float)DTid.x;
    }
)";

int main()
{
    const UINT numElements = 16;

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
    ComPtr<ID3D12Resource> stagingBuffer;
    ComPtr<ID3D12Resource> uavBuffer;

    // Create the device
    THROW_IF_FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));

    // Create a command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    THROW_IF_FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

    THROW_IF_FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&commandAllocator)));
    THROW_IF_FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

    // Create fence
    THROW_IF_FAILED(device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

    // Create output buffer (UAV)
    // Create a buffer to be used as a UAV
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = numElements * sizeof(float); // Total size of the buffer
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_RESOURCE_DESC stagingBufferDesc = {};
    stagingBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    stagingBufferDesc.Width = sizeof(float) * numElements; // The size of the buffer.
    stagingBufferDesc.Height = 1;
    stagingBufferDesc.DepthOrArraySize = 1;
    stagingBufferDesc.MipLevels = 1;
    stagingBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    stagingBufferDesc.SampleDesc.Count = 1;
    stagingBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES stagingHeapProps = {};
    stagingHeapProps.Type = D3D12_HEAP_TYPE_READBACK; // This is a readback buffer.
    stagingHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    stagingHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    THROW_IF_FAILED(device->CreateCommittedResource(
        &stagingHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &stagingBufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, // Initial state for copying
        nullptr,
        IID_PPV_ARGS(&stagingBuffer)));

    THROW_IF_FAILED(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&uavBuffer)));

    D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {};
    uavHeapDesc.NumDescriptors = 1; // We are creating one UAV
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

    CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(uavHeap->GetCPUDescriptorHandleForHeapStart());
    device->CreateUnorderedAccessView(uavBuffer.Get(), nullptr, &uavDesc, uavHandle);

    THROW_IF_FAILED(D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &shaderBlob, nullptr));

    D3D12_ROOT_PARAMETER root_params[1] = {};

    D3D12_DESCRIPTOR_RANGE desc_range = {};
    desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    desc_range.NumDescriptors = 1;
    desc_range.BaseShaderRegister = 0; // u0

    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_params[0].DescriptorTable.NumDescriptorRanges = 1;
    root_params[0].DescriptorTable.pDescriptorRanges = &desc_range;

    D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
    root_sig_desc.NumParameters = 1;
    root_sig_desc.pParameters = root_params;
    root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rootSignatureBlob;
    THROW_IF_FAILED(D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSignatureBlob, nullptr));
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

    commandList->Dispatch(numElements, 1, 1);

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(uavBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));
    commandList->CopyResource(stagingBuffer.Get(), uavBuffer.Get());
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(uavBuffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    commandList->Close();

    ID3D12CommandList* pCommandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(ARRAYSIZE(pCommandLists), pCommandLists);

    commandQueue->Signal(fence.Get(), ++fenceValue);

    // Check if the fence has already been signaled.
    if (fence->GetCompletedValue() < fenceValue) {
        // Wait for the fence to reach the signal value before proceeding
        HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        fence->SetEventOnCompletion(fenceValue, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    // Map the staging buffer and read the data
    float* data = nullptr;
    stagingBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
    for (size_t i = 0; i < numElements; ++i) {
        std::cout << "Result[" << i << "] = " << data[i] << std::endl;
    }
    stagingBuffer->Unmap(0, nullptr);

    return 0;
}
