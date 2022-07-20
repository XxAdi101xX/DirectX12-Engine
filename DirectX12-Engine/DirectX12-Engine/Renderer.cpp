#include "pch.h"
#include "Renderer.h"

#include <array>
#include <iostream>
#include <fstream>

#include "BasicReaderWriter.h"

Renderer::Renderer()
{
    initializeCoreApi();
    initializeResources();
}

Renderer::~Renderer()
{
}

void Renderer::cleanUp()
{
    waitForGpu();
    CloseHandle(m_fenceEvent);
}

void Renderer::render()
{
    // Records the commands that are to be called per frame
    populateCommandList();

    // Execute the command list.
    ID3D12CommandList *ppGraphicsCommandLists[] = { m_graphicsCommandList.get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppGraphicsCommandLists), ppGraphicsCommandLists);

    // Present the frame.
    winrt::check_hresult(m_swapChain->Present(1, 0));

    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    winrt::check_hresult(m_commandQueue->Signal(m_fence.get(), currentFenceValue));

    // Update the frame index.
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        winrt::check_hresult(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}

void Renderer::resize(UINT width, UINT height)
{
    // TODO
}

struct
    __declspec(uuid("45D64A29-A63E-4CB6-B498-5781D298CB4F"))
    __declspec(novtable)
    ICoreWindowInterop : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE get_WindowHandle(HWND *hwnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MessageHandled(unsigned char value) = 0;
};

void Renderer::initializeCoreApi()
{
    UINT dxgiFactoryFlags = 0;

    // Enable debug layer
#if defined(_DEBUG)
    winrt::check_hresult(D3D12GetDebugInterface(__uuidof(m_debugController), m_debugController.put_void()));
    m_debugController->EnableDebugLayer();
    m_debugController->SetEnableGPUBasedValidation(true);
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    // Create the factory
    winrt::check_hresult(CreateDXGIFactory2(dxgiFactoryFlags, __uuidof(m_factory), m_factory.put_void()));

    // Create Adapter
    for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != m_factory->EnumAdapters1(adapterIndex, m_adapter.put()); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc = {};
        m_adapter->GetDesc1(&desc);

        // Skip the Basic Render Driver adapter.
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            m_adapter = nullptr; // Prepare to reseat internal raw pointer
            continue;
        }

        // Select a device that supports D3D12
        winrt::check_hresult(D3D12CreateDevice(m_adapter.get(), D3D_FEATURE_LEVEL_12_1, _uuidof(m_device), m_device.put_void()));
        break;
    }

    // Create debug device
#if defined(_DEBUG)
    winrt::check_hresult(m_device->QueryInterface(m_debugDevice.put()));
#endif

    // Create command queue
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
    commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    winrt::check_hresult(m_device->CreateCommandQueue(&commandQueueDesc, __uuidof(m_commandQueue), m_commandQueue.put_void()));

    // Create fence
    winrt::check_hresult(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(m_fence), m_fence.put_void()));

    // Setup swapchain
    // TODO: refactor this to support window resizing
    setupSwapchain(winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread().Bounds().Width, winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread().Bounds().Height);

    // Create render target view descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
    rtvDescriptorHeapDesc.NumDescriptors = FrameCount;
    rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    winrt::check_hresult(m_device->CreateDescriptorHeap(&rtvDescriptorHeapDesc, __uuidof(m_rtvHeap), m_rtvHeap.put_void()));
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create frame resources
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < FrameCount; ++i)
    {
        // Create a RTV for each frame
        winrt::check_hresult(m_swapChain->GetBuffer(i, __uuidof(m_renderTargets[i]), m_renderTargets[i].put_void()));
        m_device->CreateRenderTargetView(m_renderTargets[i].get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;

        // Create command allocator
        winrt::check_hresult(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(m_commandAllocators[i]), m_commandAllocators[i].put_void()));
    }
}

void Renderer::initializeResources()
{
    // Create the root signature
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureDataRootSignature = {};
    featureDataRootSignature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    winrt::check_hresult(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureDataRootSignature, sizeof(featureDataRootSignature)));

    // Descriptors
    std::array<D3D12_DESCRIPTOR_RANGE1, 1> descriptorRanges;
    descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    descriptorRanges[0].NumDescriptors = 1;
    descriptorRanges[0].BaseShaderRegister = 0;
    descriptorRanges[0].RegisterSpace = 0;
    descriptorRanges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    descriptorRanges[0].OffsetInDescriptorsFromTableStart = 0;

    // Groups of GPU Resources
    std::array<D3D12_ROOT_PARAMETER1, 1> rootParameters;
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = descriptorRanges.size();
    rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRanges.data();

    // Allow input layout and deny uneccessary access to hull, domain and geometry shaders
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    // Overall Layout
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSignatureDesc.Desc_1_1.Flags = rootSignatureFlags;
    rootSignatureDesc.Desc_1_1.NumParameters = rootParameters.size();
    rootSignatureDesc.Desc_1_1.pParameters = rootParameters.data();
    rootSignatureDesc.Desc_1_1.NumStaticSamplers = 0;
    rootSignatureDesc.Desc_1_1.pStaticSamplers = nullptr;

    winrt::com_ptr<ID3DBlob> signature;
    winrt::com_ptr<ID3DBlob> error;

    try
    {
        winrt::check_hresult(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, signature.put(), error.put()));
        winrt::check_hresult(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), __uuidof(m_rootSignature), m_rootSignature.put_void()));
    }
    catch (std::exception e)
    {
        const char *errStr = (const char *)error->GetBufferPointer();
        std::cout << errStr << std::endl;
        throw e;
    }

    // Load Shaders
    //// Get current working directory
    //char pBuf[1024];
    //_getcwd(pBuf, 1024);
    //OutputDebugString(L"test\n");
    // TODO: dynamically get compile path
    std::wstring baseCompilePathW = L"C:\\Users\\adi_1\\Documents\\Github\\DirectX12-Engine\\DirectX12-Engine\\x64\\Debug\\AppX";

    std::wstring vertCompiledPathW = baseCompilePathW + L"\\VertexShader.cso";
    std::wstring pixelCompiledPathW = baseCompilePathW + L"\\PixelShader.cso";
    BasicReaderWriter m_basicReaderWriter;
    std::vector<byte> vertexShaderBytecode{ m_basicReaderWriter.ReadData(vertCompiledPathW.c_str()) };
    std::vector<byte> pixelShaderBytecode{ m_basicReaderWriter.ReadData(pixelCompiledPathW.c_str()) };

    D3D12_SHADER_BYTECODE vsBytecode;
    D3D12_SHADER_BYTECODE psBytecode;
    vsBytecode.pShaderBytecode = vertexShaderBytecode.data();
    vsBytecode.BytecodeLength = vertexShaderBytecode.size();
    psBytecode.pShaderBytecode = pixelShaderBytecode.data();
    psBytecode.BytecodeLength = pixelShaderBytecode.size();

    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_rootSignature.get();
    psoDesc.VS = vsBytecode;
    psoDesc.PS = psBytecode;

    D3D12_RASTERIZER_DESC rasterDesc;
    rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterDesc.DepthClipEnable = TRUE;
    rasterDesc.MultisampleEnable = FALSE;
    rasterDesc.AntialiasedLineEnable = FALSE;
    rasterDesc.ForcedSampleCount = 0;
    rasterDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    psoDesc.RasterizerState = rasterDesc;

    D3D12_BLEND_DESC blendDesc;
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc = {
        FALSE,
        FALSE,
        D3D12_BLEND_ONE,
        D3D12_BLEND_ZERO,
        D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE,
        D3D12_BLEND_ZERO,
        D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL,
    };
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        blendDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;

    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    winrt::check_hresult(m_device->CreateGraphicsPipelineState(&psoDesc, __uuidof(m_pipelineState), m_pipelineState.put_void()));


    // Create the command list.
    winrt::check_hresult(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].get(), m_pipelineState.get(), __uuidof(m_graphicsCommandList), m_graphicsCommandList.put_void()));
    winrt::check_hresult(m_graphicsCommandList->Close());

    // Create the descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    winrt::check_hresult(m_device->CreateDescriptorHeap(&heapDesc, __uuidof(m_CbvSrvUavHeap), m_CbvSrvUavHeap.put_void()));

    // Create the Constant buffer
    //m_uniformBufferData.resize(winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread().Bounds().Width * winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread().Bounds().Height);
    m_uniformBufferData.resize(64); // TODO CHANGE THIS
    std::fill(m_uniformBufferData.begin(), m_uniformBufferData.end(), 1.0f);

    float a = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread().Bounds().Width;
    float b = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread().Bounds().Height;

    // Note: using upload heaps to transfer static data like vert
    // buffers is not recommended. Every time the GPU needs it, the
    // upload heap will be marshalled over. Please read up on Default
    // Heap usage. An upload heap is used here for code simplicity and
    // because there are very few verts to actually transfer.
    D3D12_HEAP_PROPERTIES heapProps;
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    const size_t uniformBufferSize = sizeof(m_uniformBufferData[0]) * m_uniformBufferData.size();

    D3D12_RESOURCE_DESC uboResourceDesc;
    uboResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uboResourceDesc.Alignment = 0;
    uboResourceDesc.Width = (uniformBufferSize + 255) & ~255;
    uboResourceDesc.Height = 1;
    uboResourceDesc.DepthOrArraySize = 1;
    uboResourceDesc.MipLevels = 1;
    uboResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    uboResourceDesc.SampleDesc.Count = 1;
    uboResourceDesc.SampleDesc.Quality = 0;
    uboResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uboResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    winrt::check_hresult(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &uboResourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        __uuidof(m_uniformBuffer), m_uniformBuffer.put_void()));

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = m_uniformBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = (uniformBufferSize + 255) & ~255; // CB size is required to be 256-byte aligned.

    D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle(m_CbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
    cbvHandle.ptr = cbvHandle.ptr + m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * 0;

    m_device->CreateConstantBufferView(&cbvDesc, cbvHandle);

    // We do not intend to read from this resource on the CPU. (End is less than or equal to begin)
    D3D12_RANGE constantBufferReadRange;
    constantBufferReadRange.Begin = 0;
    constantBufferReadRange.End = 0;

    winrt::check_hresult(m_uniformBuffer->Map(0, &constantBufferReadRange,reinterpret_cast<void **>(&m_mappedUniformBuffer)));
    memcpy(m_mappedUniformBuffer, m_uniformBufferData.data(), uniformBufferSize);
    m_uniformBuffer->Unmap(0, &constantBufferReadRange);

    // Create the vertex buffer.
    const UINT vertexBufferSize = sizeof(mVertexBufferData);

    // Note: using upload heaps to transfer static data like vert buffers is
    // not recommended. Every time the GPU needs it, the upload heap will be
    // marshalled over. Please read up on Default Heap usage. An upload heap
    // is used here for code simplicity and because there are very few verts
    // to actually transfer.
    D3D12_HEAP_PROPERTIES vertexHeapProps;
    vertexHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    vertexHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    vertexHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    vertexHeapProps.CreationNodeMask = 1u;
    vertexHeapProps.VisibleNodeMask = 1u;

    D3D12_RESOURCE_DESC vertexBufferResourceDesc;
    vertexBufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vertexBufferResourceDesc.Alignment = 0u;
    vertexBufferResourceDesc.Width = vertexBufferSize;
    vertexBufferResourceDesc.Height = 1u;
    vertexBufferResourceDesc.DepthOrArraySize = 1u;
    vertexBufferResourceDesc.MipLevels = 1u;
    vertexBufferResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    vertexBufferResourceDesc.SampleDesc.Count = 1u;
    vertexBufferResourceDesc.SampleDesc.Quality = 0u;
    vertexBufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    vertexBufferResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    winrt::check_hresult(m_device->CreateCommittedResource(
        &vertexHeapProps, D3D12_HEAP_FLAG_NONE, &vertexBufferResourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        __uuidof(m_vertexBuffer), m_vertexBuffer.put_void())
    );

    // Copy the triangle data to the vertex buffer.
    UINT8 *pVertexDataBegin;

    // We do not intend to read from this resource on the CPU.
    D3D12_RANGE vertexBufferReadRange;
    vertexBufferReadRange.Begin = 0;
    vertexBufferReadRange.End = 0;

    winrt::check_hresult(m_vertexBuffer->Map(0, &vertexBufferReadRange, reinterpret_cast<void **>(&pVertexDataBegin)));
    memcpy(pVertexDataBegin, mVertexBufferData, sizeof(mVertexBufferData));
    m_vertexBuffer->Unmap(0, nullptr);

    // Initialize the vertex buffer view.
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes = vertexBufferSize;

    // Create the index buffer.
    const UINT indexBufferSize = sizeof(mIndexBufferData);

    // Note: using upload heaps to transfer static data like vert buffers is
    // not recommended. Every time the GPU needs it, the upload heap will be
    // marshalled over. Please read up on Default Heap usage. An upload heap
    // is used here for code simplicity and because there are very few verts
    // to actually transfer.
    D3D12_HEAP_PROPERTIES indexHeapProps;
    indexHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    indexHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    indexHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    indexHeapProps.CreationNodeMask = 1;
    indexHeapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC indexBufferResourceDesc;
    indexBufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    indexBufferResourceDesc.Alignment = 0;
    indexBufferResourceDesc.Width = indexBufferSize;
    indexBufferResourceDesc.Height = 1;
    indexBufferResourceDesc.DepthOrArraySize = 1;
    indexBufferResourceDesc.MipLevels = 1;
    indexBufferResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    indexBufferResourceDesc.SampleDesc.Count = 1;
    indexBufferResourceDesc.SampleDesc.Quality = 0;
    indexBufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    indexBufferResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    winrt::check_hresult(m_device->CreateCommittedResource(
        &indexHeapProps, D3D12_HEAP_FLAG_NONE, &indexBufferResourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        __uuidof(m_indexBuffer), m_indexBuffer.put_void()));

    UINT8 *pIndexDataBegin;

    // We do not intend to read from this resource on the CPU.
    D3D12_RANGE indexBufferReadRange;
    indexBufferReadRange.Begin = 0;
    indexBufferReadRange.End = 0;

    winrt::check_hresult(m_indexBuffer->Map(0, &indexBufferReadRange, reinterpret_cast<void **>(&pIndexDataBegin)));
    memcpy(pIndexDataBegin, mIndexBufferData, sizeof(mIndexBufferData));
    m_indexBuffer->Unmap(0, nullptr);

    // Initialize the vertex buffer view.
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = indexBufferSize;

    // Initialize fence values
    for (UINT i = 0; i < FrameCount; ++i)
    {
        m_fenceValues[i] = 0u;
    }

    // Create an event handle to use for frame synchronization.
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        winrt::check_hresult(HRESULT_FROM_WIN32(GetLastError()));
    }

    waitForGpu();
}

// Wait for pending GPU work to complete.
void Renderer::waitForGpu()
{
    // Schedule a Signal command in the queue.
    winrt::check_hresult(m_commandQueue->Signal(m_fence.get(), m_fenceValues[m_frameIndex]));

    // Wait until the fence has been processed.
    winrt::check_hresult(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

    // Increment the fence value for the current frame.
    m_fenceValues[m_frameIndex]++;
}

void Renderer::populateCommandList()
{
    // Command list allocators can only be reset when the associated
    // command lists have finished execution on the GPU; apps should use
    // fences to determine GPU execution progress.
    winrt::check_hresult(m_commandAllocators[m_frameIndex]->Reset());

    // However, when ExecuteCommandList() is called on a particular command
    // list, that command list can then be reset at any time and must be before
    // re-recording.
    winrt::check_hresult(m_graphicsCommandList->Reset(m_commandAllocators[m_frameIndex].get(), m_pipelineState.get()));

    // Set necessary state.
    m_graphicsCommandList->SetGraphicsRootSignature(m_rootSignature.get());
    m_graphicsCommandList->RSSetViewports(1, &m_viewport);
    m_graphicsCommandList->RSSetScissorRects(1, &m_surfaceSize);

    std::array<ID3D12DescriptorHeap *, 1> pDescriptorHeaps { m_CbvSrvUavHeap.get()};
    m_graphicsCommandList->SetDescriptorHeaps(pDescriptorHeaps.size(), pDescriptorHeaps.data());
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle(m_CbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
    m_graphicsCommandList->SetGraphicsRootDescriptorTable(0, srvHandle);

    // Indicate that the back buffer will be used as a render target.
    D3D12_RESOURCE_BARRIER renderTargetBarrier;
    renderTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    renderTargetBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    renderTargetBarrier.Transition.pResource = m_renderTargets[m_frameIndex].get();
    renderTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    renderTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    renderTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_graphicsCommandList->ResourceBarrier(1, &renderTargetBarrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.ptr = rtvHandle.ptr + (m_frameIndex * m_rtvDescriptorSize);
    m_graphicsCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Record commands.
    const float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
    m_graphicsCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_graphicsCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_graphicsCommandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_graphicsCommandList->IASetIndexBuffer(&m_indexBufferView);

    m_graphicsCommandList->DrawIndexedInstanced(3, 1, 0, 0, 0);

    // Indicate that the back buffer will now be used to present.
    D3D12_RESOURCE_BARRIER presentBarrier;
    presentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    presentBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    presentBarrier.Transition.pResource = m_renderTargets[m_frameIndex].get();
    presentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    presentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    presentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_graphicsCommandList->ResourceBarrier(1, &presentBarrier);

    winrt::check_hresult(m_graphicsCommandList->Close());
}

void Renderer::setupSwapchain(UINT width, UINT height)
{

    m_surfaceSize.left = 0l;
    m_surfaceSize.top = 0l;
    m_surfaceSize.right = static_cast<LONG>(width);
    m_surfaceSize.bottom = static_cast<LONG>(height);

    m_viewport.TopLeftX = 0.0f;
    m_viewport.TopLeftY = 0.0f;
    m_viewport.Width = static_cast<float>(width);
    m_viewport.Height = static_cast<float>(height);
    m_viewport.MinDepth = 0.1f;
    m_viewport.MaxDepth = 1000.f;
    
    if (m_swapChain != nullptr)
    {
        m_swapChain->ResizeBuffers(FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    }
    else
    {
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = width;
        swapChainDesc.Height = height;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.Stereo = false;
        swapChainDesc.SampleDesc.Count = 1u;
        swapChainDesc.SampleDesc.Quality = 0u;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = FrameCount;
        swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        winrt::com_ptr<IDXGISwapChain1> swapChain1;
        //winrt::check_hresult(m_factory->CreateSwapChainForCoreWindow(
        //    m_commandQueue.get(), // For Direct3D 12, this is a pointer to a direct command queue, and not to the device.
        //    reinterpret_cast<IUnknown *>(&winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread()),
        //    &swapChainDesc,
        //    nullptr,
        //    swapChain1.put()
        //));

        // TODO: does this workout to get hwnd work directly?
        winrt::com_ptr<ICoreWindowInterop> interop;
        winrt::check_hresult(winrt::get_unknown(winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread())->QueryInterface(interop.put()));
        HWND hwnd;
        winrt::check_hresult(interop->get_WindowHandle(&hwnd));
        winrt::check_hresult(m_factory->CreateSwapChainForHwnd(
            m_commandQueue.get(), // For Direct3D 12, this is a pointer to a direct command queue, and not to the device.
            hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            swapChain1.put()
        ));
        winrt::check_hresult(swapChain1->QueryInterface(__uuidof(m_swapChain), m_swapChain.put_void()));


    }
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}
