#pragma once

class Renderer
{
public:
    Renderer();

    ~Renderer();

    // Render onto the render target
    void render();

    // Resize the window and internal data structures
    void resize(UINT width, UINT height);
    void setupSwapchain(UINT width, UINT height);

private:
    // We use this value as both the maximum number of frames queued in the GPU and the number of backbuffers in the swapchain
    static const UINT FrameCount = 2u;

    // Core structures
#if defined(_DEBUG)
    winrt::com_ptr<ID3D12Debug1> m_debugController;
    winrt::com_ptr<ID3D12DebugDevice> m_debugDevice;
#endif
    winrt::com_ptr<IDXGIFactory4> m_factory;
    winrt::com_ptr<IDXGIAdapter1> m_adapter;
    winrt::com_ptr<ID3D12Device> m_device;
    winrt::com_ptr<ID3D12CommandQueue> m_commandQueue;
    winrt::com_ptr<ID3D12CommandAllocator> m_commandAllocator[FrameCount];
    winrt::com_ptr<ID3D12GraphicsCommandList> m_graphicsCommandList;
    winrt::com_ptr<IDXGISwapChain3> m_swapChain;
    D3D12_VIEWPORT m_viewport;
    D3D12_RECT m_surfaceSize;

    UINT m_currentFrame;
    UINT m_rtvDescriptorSize;
    winrt::com_ptr<ID3D12DescriptorHeap> m_rtvHeap;
    winrt::com_ptr<ID3D12Resource> m_renderTargets[FrameCount];

    // Sync
    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    winrt::com_ptr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue;

    winrt::com_ptr<ID3D12RootSignature> rootSignature;

    void initializeCoreApi();
    void initializeResources();
};