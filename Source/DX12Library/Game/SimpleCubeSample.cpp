#include "SimpleCubeSample.h"

namespace DX12Library
{
	SimpleCubeSample::SimpleCubeSample(_In_ PCWSTR pszGameName)
		: GameSample(pszGameName)
		, m_fenceEvent()
		, m_fenceValue()
		, m_frameIndex(0)
		, m_rtvDescriptorSize(0)
		, m_vertexBufferView()
		, m_indexBufferView()
		, m_modelMatrix(XMMatrixIdentity())
		, m_viewMatrix()
		, m_projectionMatrix()
	{
	}

	SimpleCubeSample::~SimpleCubeSample()
	{
	}

	void SimpleCubeSample::InitDevice()
	{
        RECT rc;
        GetClientRect(m_mainWindow->GetWindow(), &rc);
        UINT uWidth = static_cast<UINT>(rc.right - rc.left);
        UINT uHeight = static_cast<UINT>(rc.bottom - rc.top);

        {
            m_viewport.TopLeftX = 0.0f;
            m_viewport.TopLeftY = 0.0f;
            m_viewport.Width = static_cast<FLOAT>(uWidth);
            m_viewport.Height = static_cast<FLOAT>(uHeight);

            m_scissorRect.left = 0;
            m_scissorRect.top = 0;
            m_scissorRect.right = static_cast<LONG>(uWidth);
            m_scissorRect.bottom = static_cast<LONG>(uHeight);
        }

        // Load the rendering pipeline dependencies.
        UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
        // Enable the debug layer (requires the Graphics Tools "optional feature").
        // NOTE: Enabling the debug layer after device creation will invalidate the active device.
        {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();

                // Enable additional debug layers.
                dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }
#endif

        ComPtr<IDXGIFactory4> factory;
        ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

        ComPtr<IDXGIAdapter1> hardwareAdapter;
        ComPtr<IDXGIFactory6> factory6;
        if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory6))))
        {
            for (UINT adapterIndex = 0;
                SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                    adapterIndex,
                    DXGI_GPU_PREFERENCE_UNSPECIFIED,
                    IID_PPV_ARGS(&hardwareAdapter)));
                ++adapterIndex)
            {
                DXGI_ADAPTER_DESC1 desc;
                hardwareAdapter->GetDesc1(&desc);

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                {
                    // Don't select the Basic Render Driver adapter.
                    // If you want a software adapter, pass in "/warp" on the command line.
                    continue;
                }

                // Check to see whether the adapter supports Direct3D 12, but don't create the
                // actual device yet.
                if (SUCCEEDED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
                {
                    break;
                }
            }
        }

        if (hardwareAdapter.Get() == nullptr)
        {
            for (UINT adapterIndex = 0; SUCCEEDED(factory->EnumAdapters1(adapterIndex, &hardwareAdapter)); ++adapterIndex)
            {
                DXGI_ADAPTER_DESC1 desc;
                hardwareAdapter->GetDesc1(&desc);

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                {
                    // Don't select the Basic Render Driver adapter.
                    // If you want a software adapter, pass in "/warp" on the command line.
                    continue;
                }

                // Check to see whether the adapter supports Direct3D 12, but don't create the
                // actual device yet.
                if (SUCCEEDED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
                {
                    break;
                }
            }
        }

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
        ));

        // Describe and create the command queue.
        D3D12_COMMAND_QUEUE_DESC queueDesc =
        {
            .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
            .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
        };

        ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

        // Describe and create the swap chain.
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc =
        {
            .Width = uWidth,
            .Height = uHeight,
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .SampleDesc = {.Count = 1 },
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = FRAMECOUNT,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD
        };

        ComPtr<IDXGISwapChain1> swapChain;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(
            m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
            m_mainWindow->GetWindow(),
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain
        ));

        // This sample does not support fullscreen transitions.
        ThrowIfFailed(factory->MakeWindowAssociation(m_mainWindow->GetWindow(), DXGI_MWA_NO_ALT_ENTER));

        ThrowIfFailed(swapChain.As(&m_swapChain));
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // Create descriptor heaps.
        {
            // Describe and create a render target view (RTV) descriptor heap.
            D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc =
            {
                .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                .NumDescriptors = FRAMECOUNT,
                .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
            };
            ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

            m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            // Describe and create a depth stencil view (DSV) descriptor heap.
            D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc =
            {
                .Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                .NumDescriptors = 1,
                .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
            };
            ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));
        }

        // Create frame resources.
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

            // Create a RTV for each frame.
            for (UINT n = 0; n < FRAMECOUNT; n++)
            {
                ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
                m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
                rtvHandle.Offset(1, m_rtvDescriptorSize);
            }
        }

        ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));

        // Load the sample assets.
        // Create a root signature consisting of a descriptor table with a single CBV.
        {
            D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData =
            {
                // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
                .HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1
            };

            if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
            {
                featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
            }

            CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
            CD3DX12_ROOT_PARAMETER1 rootParameters[1];

            ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
            rootParameters[0].InitAsConstants(sizeof(XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

            // Allow input layout and deny uneccessary access to certain pipeline stages.
            D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
            rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

            ComPtr<ID3DBlob> signature;
            ComPtr<ID3DBlob> error;
            ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
            ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
        }

        // Create the pipeline state, which includes compiling and loading shaders.
        {
            ComPtr<ID3DBlob> vertexShader;
            ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
            // Enable better shader debugging with the graphics debugging tools.
            UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            UINT compileFlags = 0;
#endif

            ThrowIfFailed(D3DCompileFromFile(L"Shaders/shadersCube.hlsl", nullptr, nullptr, "VSMain", "vs_5_1", compileFlags, 0, &vertexShader, nullptr));
            ThrowIfFailed(D3DCompileFromFile(L"Shaders/shadersCube.hlsl", nullptr, nullptr, "PSMain", "ps_5_1", compileFlags, 0, &pixelShader, nullptr));

            // Define the vertex input layout.
            D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            // Describe and create the graphics pipeline state object (PSO).
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc =
            {
                .pRootSignature = m_rootSignature.Get(),
                .VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get()),
                .PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get()),
                .BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
                .SampleMask = UINT_MAX,
                .RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
                .DepthStencilState = {.DepthEnable = FALSE,
                                      .StencilEnable = FALSE},
                .InputLayout = { inputElementDescs, _countof(inputElementDescs) },
                .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                .NumRenderTargets = 1,
                .RTVFormats = { DXGI_FORMAT_R8G8B8A8_UNORM, },
                .DSVFormat = DXGI_FORMAT_D32_FLOAT,
                .SampleDesc = {.Count = 1 }
            };

            ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
        }

        // Create the command list.
        ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

        // Command lists are created in the recording state, but there is nothing
        // to record yet. The main loop expects it to be closed, so close it now.
        ThrowIfFailed(m_commandList->Close());

        // Create the vertex buffer.
        {
            // Define the geometry for a triangle.
            VertexPosColor vertices[] =
            {
                { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, 0.0f) }, // 0
                { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) }, // 1
                { XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f) }, // 2
                { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) }, // 3
                { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) }, // 4
                { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 1.0f) }, // 5
                { XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f) }, // 6
                { XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) }  // 7
            };

            const UINT vertexBufferSize = sizeof(vertices);

            // Note: using upload heaps to transfer static data like vert buffers is not 
            // recommended. Every time the GPU needs it, the upload heap will be marshalled 
            // over. Please read up on Default Heap usage. An upload heap is used here for 
            // code simplicity and because there are very few verts to actually transfer.
            CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
            CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
            ThrowIfFailed(m_device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &resourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&m_vertexBuffer)));

            // Copy the triangle data to the vertex buffer.
            UINT8* pVertexDataBegin;
            CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
            ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
            memcpy(pVertexDataBegin, vertices, sizeof(vertices));
            m_vertexBuffer->Unmap(0, nullptr);

            // Initialize the vertex buffer view.
            m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
            m_vertexBufferView.StrideInBytes = sizeof(VertexPosColor);
            m_vertexBufferView.SizeInBytes = vertexBufferSize;
        }

        // Create the index buffer.
        {
            WORD indicies[36] =
            {
                0, 1, 2, 0, 2, 3,
                4, 6, 5, 4, 7, 6,
                4, 5, 1, 4, 1, 0,
                3, 2, 6, 3, 6, 7,
                1, 5, 6, 1, 6, 2,
                4, 0, 3, 4, 3, 7
            };

            const UINT indexBufferSize = sizeof(indicies);

            CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
            CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
            ThrowIfFailed(m_device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &resourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&m_indexBuffer)));

            UINT8* pIndexDataBegin;
            CD3DX12_RANGE readRange(0, 0);
            ThrowIfFailed(m_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)));
            memcpy(pIndexDataBegin, indicies, sizeof(indicies));
            m_indexBuffer->Unmap(0, nullptr);

            m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
            m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
            m_indexBufferView.SizeInBytes = sizeof(indicies);
        }

        // Create a depth buffer.
        {
            D3D12_CLEAR_VALUE optimizedClearValue =
            {
                .Format = DXGI_FORMAT_D32_FLOAT,
                .DepthStencil = { 1.0f, 0 }
            };

            CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
            CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, uWidth, uHeight, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
            ThrowIfFailed(m_device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &resourceDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &optimizedClearValue,
                IID_PPV_ARGS(&m_depthBuffer)));

            // Create a depth-stencil view
            {
                D3D12_DEPTH_STENCIL_VIEW_DESC dsv =
                {
                    .Format = DXGI_FORMAT_D32_FLOAT,
                    .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
                    .Flags = D3D12_DSV_FLAG_NONE,
                    .Texture2D = {.MipSlice = 0 }
                };
                m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsv, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
            }
        }

        // Create synchronization objects and wait until assets have been uploaded to the GPU.
        {
            ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
            m_fenceValue = 1;

            // Create an event handle to use for frame synchronization.
            m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (m_fenceEvent == nullptr)
            {
                ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
            }

            // Wait for the command list to execute; we are reusing the same command 
            // list in our main loop but for now, we just want to wait for setup to 
            // complete before continuing.
            // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
            // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
            // sample illustrates how to use fences for efficient resource usage and to
            // maximize GPU utilization.

            // Signal and increment the fence value.
            const UINT64 fence = m_fenceValue;
            ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
            m_fenceValue++;

            // Wait until the previous frame is finished.
            if (m_fence->GetCompletedValue() < fence)
            {
                ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
                WaitForSingleObject(m_fenceEvent, INFINITE);
            }

            m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        }
	}

	void SimpleCubeSample::CleanupDevice()
	{
		// Ensure that the GPU is no longer referencing resources that are about to be
		// cleaned up by the destructor.

		// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
		// This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
		// sample illustrates how to use fences for efficient resource usage and to
		// maximize GPU utilization.

		// Signal and increment the fence value.
		const UINT64 fence = m_fenceValue;
		ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
		++m_fenceValue;

		// Wait until the previous frame is finished.
		if (m_fence->GetCompletedValue() < fence)
		{
			ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

		CloseHandle(m_fenceEvent);
	}

	void SimpleCubeSample::Update(_In_ FLOAT deltaTime)
	{
        // Update the model matrix.
        const XMVECTOR rotationAxis = XMVectorSet(0, 1, 1, 0);
        m_modelMatrix *= XMMatrixRotationAxis(rotationAxis, -2.0f * deltaTime);

        // Update the view matrix.
        const XMVECTOR eyePosition = XMVectorSet(0, 0, -10, 1);
        const XMVECTOR focusPoint = XMVectorSet(0, 0, 0, 1);
        const XMVECTOR upDirection = XMVectorSet(0, 1, 0, 0);
        m_viewMatrix = XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);

        RECT rc;
        GetClientRect(m_mainWindow->GetWindow(), &rc);
        FLOAT uWidth = static_cast<FLOAT>(rc.right - rc.left);
        FLOAT uHeight = static_cast<FLOAT>(rc.bottom - rc.top);
        // Update the projection matrix.
        float aspectRatio = uWidth / uHeight;
        m_projectionMatrix = XMMatrixPerspectiveFovLH(XMConvertToRadians(45.0f), aspectRatio, 0.1f, 100.0f);
	}

	void SimpleCubeSample::Render()
	{
        // Record all the commands we need to render the scene into the command list.
        // Command list allocators can only be reset when the associated 
        // command lists have finished execution on the GPU; apps should use 
        // fences to determine GPU execution progress.
        ThrowIfFailed(m_commandAllocator->Reset());

        // However, when ExecuteCommandList() is called on a particular command 
        // list, that command list can then be reset at any time and must be before 
        // re-recording.
        ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

        // Set necessary state.
        m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

        // Update the MVP matrix
        XMMATRIX mvpMatrix = XMMatrixMultiply(m_modelMatrix, m_viewMatrix);
        mvpMatrix = XMMatrixMultiply(mvpMatrix, m_projectionMatrix);
        m_commandList->SetGraphicsRoot32BitConstants(0, sizeof(XMMATRIX) / 4, &mvpMatrix, 0);

        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissorRect);

        // Indicate that the back buffer will be used as a render target.
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commandList->ResourceBarrier(1, &barrier);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

        // Record commands.
        const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
        m_commandList->IASetIndexBuffer(&m_indexBufferView);
        m_commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);

        // Indicate that the back buffer will now be used to present.
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        m_commandList->ResourceBarrier(1, &barrier);

        ThrowIfFailed(m_commandList->Close());

        // Execute the command list.
        ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        // Present the frame.
        ThrowIfFailed(m_swapChain->Present(1, 0));

        // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
        // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
        // sample illustrates how to use fences for efficient resource usage and to
        // maximize GPU utilization.

        // Signal and increment the fence value.
        const UINT64 fence = m_fenceValue;
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
        ++m_fenceValue;

        // Wait until the previous frame is finished.
        if (m_fence->GetCompletedValue() < fence)
        {
            ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }

        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	}
}