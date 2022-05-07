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

void Renderer::render()
{
}

void Renderer::resize(UINT width, UINT height)
{
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
    for (UINT adapterIndex = 0;
        DXGI_ERROR_NOT_FOUND != m_factory->EnumAdapters1(adapterIndex, m_adapter.put());
        ++adapterIndex)
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
    std::array<D3D12_DESCRIPTOR_RANGE1, 1> ranges;
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;

    // Groups of GPU Resources
    std::array<D3D12_ROOT_PARAMETER1, 1> rootParameters;
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[0].DescriptorTable.pDescriptorRanges = ranges.data();

    // Overall Layout
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.Desc_1_1.NumParameters = 1;
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

    D3D12_SHADER_BYTECODE vsBytecode;
    D3D12_SHADER_BYTECODE psBytecode;

    vsBytecode.pShaderBytecode = vertexShaderBytecode.data();
    vsBytecode.BytecodeLength = vertexShaderBytecode.size();

    psBytecode.pShaderBytecode = pixelShaderBytecode.data();
    psBytecode.BytecodeLength = pixelShaderBytecode.size();

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

    // Create synchronization objects and wait until assets have been uploaded
    // to the GPU.
    m_fenceValue = 1;

    // Create an event handle to use for frame synchronization.
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        winrt::check_hresult(HRESULT_FROM_WIN32(GetLastError()));
    }

    // Wait for the command list to execute; we are reusing the same command
    // list in our main loop but for now, we just want to wait for setup to
    // complete before continuing.
    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValue;
    winrt::check_hresult(m_commandQueue->Signal(m_fence.get(), fence));
    m_fenceValue++;

    // Wait until the previous frame is finished.
    if (m_fence->GetCompletedValue() < fence)
    {
        winrt::check_hresult(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
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
