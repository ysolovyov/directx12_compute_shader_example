#include "pch.h"

using Microsoft::WRL::ComPtr;

const int numElements = 16;
int inputData[numElements] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
int outputData[numElements];

// const char* shaderSource = R"(
//     RWBuffer<int> srcBuffer: register(u0);
//     RWBuffer<int> dstBuffer: register(u1);

//     [numthreads(1, 1, 1)]
//     void main(uint3 groupID : SV_GroupID, uint3 tid : SV_DispatchThreadID, uint3 localTID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
//     {
//         const int index = tid.x;
//         dstBuffer[index] = srcBuffer[index] + 10;
//     }
// )";

// Global DirectX 12 objects
ComPtr<ID3D12Device> pDevice;
ComPtr<ID3D12CommandQueue> pCommandQueue;
ComPtr<ID3D12CommandAllocator> pCommandAllocator;
ComPtr<ID3D12GraphicsCommandList> pCommandList;
ComPtr<ID3D12RootSignature> pRootSignature;
ComPtr<ID3D12PipelineState> pPipelineState;
ComPtr<ID3D12Resource> pInputBuffer;
ComPtr<ID3D12Resource> pOutputBuffer;
ComPtr<ID3D12DescriptorHeap> pUavHeap;

void InitD3D() {
    printf("InitD3D");

#if defined(_DEBUG)
    // Enable the D3D12 debug layer.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
        }
    }
#endif

    // Create D3D12 Device
    THROW_IF_FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&pDevice)));

    // Create Command Queue
    D3D12_COMMAND_QUEUE_DESC cqDesc = {};
    pDevice->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&pCommandQueue));

    // Create Command Allocator and Command List
    pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&pCommandAllocator));
    pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, pCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&pCommandList));
}

// Create buffers
void CreateBuffers() {
    printf("CreateBuffers\n");
    // Create input buffer
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = numElements * sizeof(int);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&pInputBuffer));

    // Upload data to input buffer
    // int* pData;
    // pInputBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData));
    // memcpy(pData, inputData, sizeof(inputData));
    // pInputBuffer->Unmap(0, nullptr);

    // Create output buffer
    bufferDesc.Width = numElements * sizeof(int);
    pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&pOutputBuffer));
}

void CreateRootSignature()
{
    printf("CreateRootSignature\n");
    
    CD3DX12_DESCRIPTOR_RANGE ranges[1];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

    CD3DX12_ROOT_PARAMETER rootParameters[1];
    rootParameters[0].InitAsDescriptorTable(1, ranges, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
    computeRootSignatureDesc.Init(_countof(rootParameters), rootParameters, 0, nullptr);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;

    THROW_IF_FAILED(D3D12SerializeRootSignature(&computeRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, signature.GetAddressOf(), nullptr));

    THROW_IF_FAILED(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRootSignature)));
}

void CreateHeap()
{
    printf("CreateHeap\n");
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};

    desc.NumDescriptors = 2;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    THROW_IF_FAILED(pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pUavHeap)));
}

void CreateViews()
{
    printf("CreateViews\n");
    UINT uav_descriptor_size = pDevice->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = numElements;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = pUavHeap->GetCPUDescriptorHandleForHeapStart();

    pDevice->CreateUnorderedAccessView(pInputBuffer.Get(), nullptr, &uavDesc, uavHandle);
    uavHandle.ptr += uav_descriptor_size;
    pDevice->CreateUnorderedAccessView(pOutputBuffer.Get(), nullptr, &uavDesc, uavHandle);
}

void CreatePipelineState()
{
    printf("CreatePipelineState\n");
    ComPtr<ID3DBlob> pCSBlob = nullptr;
    //ComPtr<ID3DBlob> pErrorBlob = nullptr;

    // THROW_IF_FAILED(D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, pCSBlob.GetAddressOf(), pErrorBlob.GetAddressOf()));

    THROW_IF_FAILED(D3DReadFileToBlob(L"compute.cso", &pCSBlob));

    // Create compute pipeline state
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = pRootSignature.Get();
    psoDesc.CS = CD3DX12_SHADER_BYTECODE(pCSBlob.Get());

    THROW_IF_FAILED(pDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pPipelineState)));
}

void DispatchComputeShader() {
    printf("DispatchComputeShader\n");

    // Set descriptor heaps for SRV and UAV
    ID3D12DescriptorHeap* ppHeaps[] = { pUavHeap.Get() };
    printf("SetDescriptorHeaps\n");
    pCommandList->SetDescriptorHeaps(ARRAYSIZE(ppHeaps), ppHeaps);

    // Step 16: Set up Command List
    printf("SetComputeRootSignature\n");
    pCommandList->SetComputeRootSignature(pRootSignature.Get());
    printf("SetPipelineState\n");
    pCommandList->SetPipelineState(pPipelineState.Get());

    // Set UAV
    printf("SetComputeRootDescriptorTable\n");
    pCommandList->SetComputeRootDescriptorTable(0, pUavHeap->GetGPUDescriptorHandleForHeapStart());

    printf("Dispatch\n");
    // Dispatch compute shader
    pCommandList->Dispatch(numElements, 1, 1);

    printf("Close");
    // Step 17: Close Command List and Execute
    pCommandList->Close();

    // Execute Command List
    ID3D12CommandList* ppCommandLists[] = { pCommandList.Get() };
    printf("ExecuteCommandLists\n");
    pCommandQueue->ExecuteCommandLists(ARRAYSIZE(ppCommandLists), ppCommandLists);
}

void RetrieveResults() {
    // Map output buffer 1 to read back results
    pOutputBuffer->Map(0, nullptr, reinterpret_cast<void**>(&outputData));

    printf("RetrieveResults\n");

    for (int i = 0; i < numElements; ++i) {
        printf("Output: %d\n", outputData[i]);
    }

    pOutputBuffer->Unmap(0, nullptr);
}

int main() {
    try {
        InitD3D();
        CreateBuffers();
        CreateRootSignature();
        CreateHeap();
        CreateViews();
        CreatePipelineState();
        DispatchComputeShader();
        RetrieveResults();
    } catch (std::exception& e) {
        printf("Error: %s\n", e.what());
    } catch (...) {
        printf("Unknown error");
    }

    return 0;
}