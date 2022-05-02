#pragma once

using namespace winrt::Windows::UI::Core;

class Renderer
{
public:
    Renderer(CoreWindow &window);

    ~Renderer();

    // Render onto the render target
    void render();

    // Resize the window and internal data structures
    void resize(unsigned width, unsigned height);

private:
    CoreWindow &mWindow;

#if defined(_DEBUG)
    winrt::com_ptr<ID3D12Debug1> mDebugController;
    winrt::com_ptr<ID3D12DebugDevice> mDebugDevice;
#endif
    winrt::com_ptr<IDXGIFactory4> mFactory;
    winrt::com_ptr<IDXGIAdapter1> mAdapter;
    winrt::com_ptr<ID3D12Device> mDevice;
};