#include "pch.h"
#include "Renderer.h"

Renderer::Renderer(CoreWindow &window) : mWindow(window)
{
	UINT dxgiFactoryFlags = 0;

    // Enable debug layer
#if defined(_DEBUG)
    winrt::com_ptr<ID3D12Debug> debugController0;
    winrt::check_hresult(D3D12GetDebugInterface(__uuidof(debugController0), debugController0.put_void()));
    debugController0->QueryInterface(__uuidof(mDebugController), mDebugController.put_void());

    mDebugController->EnableDebugLayer();
    mDebugController->SetEnableGPUBasedValidation(true);

    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    // Create the factory
    winrt::check_hresult(CreateDXGIFactory2(dxgiFactoryFlags,  __uuidof(mFactory), mFactory.put_void()));

    // Create Adapter
    for (UINT adapterIndex = 0;
        DXGI_ERROR_NOT_FOUND != mFactory->EnumAdapters1(adapterIndex, mAdapter.put());
        ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        mAdapter->GetDesc1(&desc);

        // Skip the Basic Render Driver adapter.
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            mAdapter = nullptr; // Prepare to reseat internal raw pointer
            continue;
        }

        // Select a device that supports D3D12
        winrt::check_hresult(D3D12CreateDevice(mAdapter.get(), D3D_FEATURE_LEVEL_12_0, _uuidof(mDevice), mDevice.put_void()));
        break;
    }

    // Create debug device
#if defined(_DEBUG)
    winrt::check_hresult(mDevice->QueryInterface(mDebugDevice.put()));
#endif
}

Renderer::~Renderer()
{
}

void Renderer::render()
{
}

void Renderer::resize(unsigned width, unsigned height)
{
}
