#include <array>

#include "pch.h"
#include "Renderer.h"

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
        winrt::check_hresult(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(m_commandAllocator[i]), m_commandAllocator[i].put_void()));
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
    winrt::check_hresult(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, signature.put(), error.put()));
    winrt::check_hresult(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), __uuidof(rootSignature), rootSignature.put_void()));
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
