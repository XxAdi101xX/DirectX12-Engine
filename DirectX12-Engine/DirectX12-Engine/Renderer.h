#pragma once

class Renderer
{
public:
    Renderer();

    ~Renderer();

    void cleanUp();

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
    winrt::com_ptr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
    winrt::com_ptr<ID3D12GraphicsCommandList> m_graphicsCommandList;
    winrt::com_ptr<IDXGISwapChain3> m_swapChain;

    // Resources
    D3D12_VIEWPORT m_viewport;
    D3D12_RECT m_surfaceSize;

    winrt::com_ptr<ID3D12Resource> m_vertexBuffer;
    winrt::com_ptr<ID3D12Resource> m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;

    // Frame resources
    UINT m_currentFrame;
    UINT m_rtvDescriptorSize;
    winrt::com_ptr<ID3D12DescriptorHeap> m_rtvHeap;
    winrt::com_ptr<ID3D12Resource> m_renderTargets[FrameCount];

    // Sync
    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    winrt::com_ptr<ID3D12Fence> m_fence;
    UINT64 m_fenceValues[FrameCount];

    winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
    winrt::com_ptr<ID3D12PipelineState> m_pipelineState;

    struct Vertex
    {
        float position[3];
        float color[4];
    };

    Vertex mVertexBufferData[3] =
    {
        {{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0, 1.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}
    };

    uint32_t mIndexBufferData[3] = { 0u, 1u, 2u };

    std::vector<float> m_uniformBufferData;
    winrt::com_ptr<ID3D12Resource> m_uniformBuffer;
    winrt::com_ptr<ID3D12DescriptorHeap> m_CbvSrvUavHeap;
    UINT8 *m_mappedUniformBuffer;

    void initializeCoreApi();
    void initializeResources();
    void populateCommandList();

    void waitForGpu();
};