#include <windows.h>
#include "d3dx12.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx12.h"
#include <sstream>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <filesystem>

#include <DirectXMath.h>
#include "OBJLoader.h"
using namespace DirectX;

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib") 

using Microsoft::WRL::ComPtr;
const UINT WindowWidth = 1280;
const UINT WindowHeight = 720;

HWND hWnd;

ComPtr<ID3D12Device> g_device; // gpu
ComPtr<IDXGISwapChain3> g_swapChain; // back buffering
ComPtr<ID3D12CommandQueue> g_commandQueue; // submit commands for the GPU to execute
ComPtr<ID3D12CommandAllocator> g_commandAllocator; // memory for a batch of commands
ComPtr<ID3D12GraphicsCommandList> g_commandList;

ComPtr<ID3D12DescriptorHeap> g_rtvHeap; // a heap to store descriptors
UINT g_rtvDescriptorSize = 0; // size of a single descriptor on GPU
ComPtr<ID3D12Resource> g_renderTargets[2]; // double buffering
UINT g_currentBackBuffer = 0;

// synchronization
ComPtr<ID3D12Fence> g_fence;
UINT64 g_fenceValue = 0;
HANDLE g_fenceEvent; // to tell CPU to wait for GPU

ComPtr<ID3D12RootSignature> g_rootSignature; // defines resources shaders need
ComPtr<ID3D12PipelineState> g_pipelineState;

XMFLOAT4X4 g_worldMatrix;
XMFLOAT4X4 g_viewMatrix;
XMFLOAT4X4 g_projectionMatrix;

ComPtr<ID3D12Resource> g_vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView;

ComPtr<ID3D12Resource> g_indexBuffer;
D3D12_INDEX_BUFFER_VIEW g_indexBufferView;
UINT g_indexCount = 0;

ComPtr<ID3D12Resource> g_matrixConstantBuffer;
ComPtr<ID3D12Resource> g_lightConstantBuffer;

UINT8* g_pMatrixConstantBufferStart = nullptr; // cpu pointer to gpu memory
UINT8* g_pLightConstantBufferStart = nullptr;

ComPtr<ID3D12Resource> g_depthBuffer;
ComPtr<ID3D12DescriptorHeap> g_dsvHeap;
D3D12_CPU_DESCRIPTOR_HANDLE g_dsvHandle;

struct CameraInput {
	bool moveForward = false;
	bool moveBackward = false;
	bool moveLeft = false;
	bool moveRight = false;
	bool moveUp = false;
	bool moveDown = false;
	bool rotateLeft = false;
	bool rotateRight = false;
	bool rotateUp = false;
	bool rotateDown = false;
};

CameraInput g_cameraInput;

XMFLOAT3 g_cameraPosition = { 0.0f, 5.0f, -15.0f };
XMFLOAT3 g_cameraTarget = { 0.0f, 0.0f, 0.0f };
XMFLOAT3 g_cameraUp = { 0.0f, 1.0f, 0.0f };
float g_cameraMoveSpeed = 5.0f;
float g_cameraRotationSpeed = 1.0f;

struct VertexTest {
	XMFLOAT3 position;  // 12 bytes
	XMFLOAT3 normal;    // 12 bytes
	XMFLOAT2 texCoord;  // 8 bytes
};

struct MatrixBuffer
{
	XMFLOAT4X4 world;
	XMFLOAT4X4 view;
	XMFLOAT4X4 projection;

	float padding[4]; // 4 floats = 16 bytes for alignment
}; MatrixBuffer g_matrixBufferData;

struct LightBuffer
{
	XMFLOAT3 lightDirection;
	float padding1;       // 4 bytes padding
	XMFLOAT3 lightColor;
	float lightIntensity;
	float ambientIntensity;
	XMFLOAT3 cameraPosition;
	float specularPower;
	float specularIntensity;
	float padding2[2];    // 8 bytes padding
}; LightBuffer g_lightBufferData;

ComPtr<ID3D12DescriptorHeap> g_ImguiSrvDescHeap;
float g_clearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };

std::vector<ComPtr<ID3D12Resource>> g_uploadResources;

struct RenderMesh {
	ComPtr<ID3D12Resource> vertexBuffer;
	ComPtr<ID3D12Resource> indexBuffer;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	UINT indexCount;
};
std::vector<RenderMesh> g_meshes;

ComPtr<ID3D12Resource> g_texture;
ComPtr<ID3D12Resource> g_textureUploadHeap;
D3D12_GPU_DESCRIPTOR_HANDLE g_textureHandle;
ComPtr<ID3D12DescriptorHeap> g_textureSrvHeap;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitD3D();
void PopulateCommandList();
void WaitForPreviousFrame();
void LoadTexture();
void CreateConstantBuffers();
template<typename T>
void CreateConstantBuffer(ComPtr<ID3D12Resource>& buffer, UINT8*& mappedData, const T& initialData);
void CreateBuffer(const void* data, UINT size, ComPtr<ID3D12Resource>& resource,
	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);
bool LoadOBJModel(const std::string& filename);
void CleanupUploadResources();
void UpdateCamera(float deltaTime);

// main entry point for windows applications
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	SetProcessDPIAware();
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	// register
	const wchar_t CLASS_NAME[] = L"dx12 window class";

	WNDCLASS wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);

	// create
	hWnd = CreateWindowEx(
		0,
		CLASS_NAME,
		L"dx12 hello world",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, WindowWidth, WindowHeight,
		NULL, NULL, hInstance, NULL
	);

	if (hWnd == NULL)
	{
		return 0;
	}

	// initialize direct3d
	InitD3D();

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	SetFocus(hWnd);

	// main loop
	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			LARGE_INTEGER frequency, lastTime, currentTime;
			QueryPerformanceFrequency(&frequency);
			QueryPerformanceCounter(&lastTime);
			float deltaTime = 0.0f;

			QueryPerformanceCounter(&currentTime);
			deltaTime = static_cast<float>(currentTime.QuadPart - lastTime.QuadPart) / frequency.QuadPart;
			lastTime = currentTime;

			// cap delta time to avoid large jumps
			if (deltaTime > 0.1f) deltaTime = 0.1f;
			UpdateCamera(deltaTime);


			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2((float)WindowWidth, (float)WindowHeight);

			static RECT rect;
			GetClientRect(hWnd, &rect);
			io.DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));

			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			ImGui::Begin("Camera Controls");
			ImGui::Text("Camera Position: (%.1f, %.1f, %.1f)",
				g_cameraPosition.x, g_cameraPosition.y, g_cameraPosition.z);
			ImGui::Text("Camera Target: (%.1f, %.1f, %.1f)",
				g_cameraTarget.x, g_cameraTarget.y, g_cameraTarget.z);
			ImGui::SliderFloat("Move Speed", &g_cameraMoveSpeed, 1.0f, 50.0f);
			ImGui::SliderFloat("Rotation Speed", &g_cameraRotationSpeed, 0.1f, 5.0f);

			// Reset button
			if (ImGui::Button("Reset Camera")) {
				g_cameraPosition = { 0.0f, 5.0f, -15.0f };
				g_cameraTarget = { 0.0f, 0.0f, 0.0f };
				g_cameraInput = {}; // Reset all input flags
			}
			ImGui::End();


			PopulateCommandList();
			ID3D12CommandList* commandLists[] = { g_commandList.Get() };
			g_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
			g_swapChain->Present(1, 0);
			WaitForPreviousFrame();
		}
	}

	CloseHandle(g_fenceEvent);
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	return 0;
}

// handles messages from the OS
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{

	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
	{
		ImGuiIO& io = ImGui::GetIO();
		if (io.WantCaptureKeyboard) {
			return true;
		}
	}

	switch (message)
	{
	case WM_KEYDOWN:
		switch (wParam)
		{
		case 'W': g_cameraInput.moveForward = true; break;
		case 'S': g_cameraInput.moveBackward = true; break;
		case 'A': g_cameraInput.moveLeft = true; break;
		case 'D': g_cameraInput.moveRight = true; break;
		case 'Q': g_cameraInput.moveUp = true; break;
		case 'E': g_cameraInput.moveDown = true; break;
		case VK_LEFT: g_cameraInput.rotateLeft = true; break;
		case VK_RIGHT: g_cameraInput.rotateRight = true; break;
		case VK_UP: g_cameraInput.rotateUp = true; break;
		case VK_DOWN: g_cameraInput.rotateDown = true; break;
		}
		break;

	case WM_KEYUP:
		switch (wParam)
		{
		case 'W': g_cameraInput.moveForward = false; break;
		case 'S': g_cameraInput.moveBackward = false; break;
		case 'A': g_cameraInput.moveLeft = false; break;
		case 'D': g_cameraInput.moveRight = false; break;
		case 'Q': g_cameraInput.moveUp = false; break;
		case 'E': g_cameraInput.moveDown = false; break;
		case VK_LEFT: g_cameraInput.rotateLeft = false; break;
		case VK_RIGHT: g_cameraInput.rotateRight = false; break;
		case VK_UP: g_cameraInput.rotateUp = false; break;
		case VK_DOWN: g_cameraInput.rotateDown = false; break;
		}
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

void CreatePipelineStateObject()
{
	HRESULT hr;

	// compile shaders
	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;
	ComPtr<ID3DBlob> errorBuffer;

	if (!std::filesystem::exists(L"VertexShader.hlsl")) {
		MessageBoxW(nullptr, L"VertexShader.hlsl not found!", L"Error", MB_OK);
		exit(1);
	}
	if (!std::filesystem::exists(L"PixelShader.hlsl")) {
		MessageBoxW(nullptr, L"PixelShader.hlsl not found!", L"Error", MB_OK);
		exit(1);
	}

	hr = D3DCompileFromFile(
		L"VertexShader.hlsl",
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"main",
		"vs_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&vertexShader,
		&errorBuffer
	);


	if (FAILED(hr))
	{
		MessageBoxA(0, (char*)errorBuffer->GetBufferPointer(), "Vertex Shader Compile Error", MB_OK);
		exit(1);
	}

	hr = D3DCompileFromFile(
		L"PixelShader.hlsl",
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"main",
		"ps_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pixelShader,
		&errorBuffer
	);
	if (FAILED(hr))
	{
		MessageBoxA(0, (char*)errorBuffer->GetBufferPointer(), "Pixel Shader Compile Error", MB_OK);
		exit(1);
	}

	// create a root signature
	// updating root parameter for imgui
	// parameter0 cbv

	D3D12_ROOT_PARAMETER rootParameters[3] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // constant buffer view
	rootParameters[0].Descriptor.ShaderRegister = 0; // b0
	rootParameters[0].Descriptor.RegisterSpace = 0;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// light cbv
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].Descriptor.ShaderRegister = 1; // b1
	rootParameters[1].Descriptor.RegisterSpace = 0;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// parameter2 descriptor table for textures
	CD3DX12_DESCRIPTOR_RANGE descriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[2].DescriptorTable.pDescriptorRanges = &descriptorRange;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // textures are usally used in pixel shaders

	D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.MipLODBias = 0;
	samplerDesc.MaxAnisotropy = 0;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplerDesc.MinLOD = 0.0f;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.ShaderRegister = 0;
	samplerDesc.RegisterSpace = 0;
	samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(3, rootParameters, 1, &samplerDesc, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	ComPtr<ID3DBlob> signature;
	D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, nullptr);
	g_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&g_rootSignature));

	// define vertex input layout
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};

	// create pso
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
	psoDesc.pRootSignature = g_rootSignature.Get();
	psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
	psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };

	CD3DX12_RASTERIZER_DESC rasterizerDesc(D3D12_DEFAULT);
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState = rasterizerDesc;

	CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);

	const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc = {
		FALSE, FALSE,
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_LOGIC_OP_NOOP,
		D3D12_COLOR_WRITE_ENABLE_ALL,
	};

	for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
		blendDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;
	}

	psoDesc.BlendState = blendDesc;
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // Must match swap chain format
	psoDesc.SampleDesc.Count = 1;

	hr = g_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_pipelineState));
	if (FAILED(hr))
	{
		MessageBox(nullptr, L"Failed to create Pipeline State Object!", L"Error", MB_OK);
		exit(1);
	}
}

void CreateAssets()
{
	g_commandList->Reset(g_commandAllocator.Get(), nullptr);
	CreateConstantBuffers();
	g_commandList->Close();

	ID3D12CommandList* ppCommandLists[] = { g_commandList.Get() };
	g_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	WaitForPreviousFrame();
}

void CreateConstantBuffers()
{
	// initialize matrix buffer
	MatrixBuffer initMatrix;
	DirectX::XMStoreFloat4x4(&initMatrix.world, XMMatrixIdentity());
	DirectX::XMStoreFloat4x4(&initMatrix.view, XMMatrixIdentity());
	DirectX::XMStoreFloat4x4(&initMatrix.projection, XMMatrixIdentity());

	CreateConstantBuffer(g_matrixConstantBuffer, g_pMatrixConstantBufferStart, initMatrix);
	g_matrixBufferData = initMatrix;

	// initialize light buffer
	LightBuffer initLight;
	initLight.lightDirection = XMFLOAT3(0.0f, -1.0f, -1.0f);
	initLight.lightColor = XMFLOAT3(1.0f, 1.0f, 1.0f);
	initLight.ambientIntensity = 0.1f;
	initLight.lightIntensity = 1.0f;
	initLight.specularPower = 32.0f;
	initLight.specularIntensity = 1.0f;
	initLight.cameraPosition = XMFLOAT3(0.0f, 0.0f, -5.0f);

	CreateConstantBuffer(g_lightConstantBuffer, g_pLightConstantBufferStart, initLight);
	g_lightBufferData = initLight;
}

void CreateBuffer(const void* data, UINT size, ComPtr<ID3D12Resource>& resource,
	D3D12_RESOURCE_STATES initialState) 
{
	auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	// create default heap resource
	auto resDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
	g_device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		initialState,
		nullptr,
		IID_PPV_ARGS(&resource)
	);

	// create upload heap for vertex data
	ComPtr<ID3D12Resource> uploadResource;
	g_device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadResource)
	);

	g_uploadResources.push_back(uploadResource);

	// map and copy data
	void* mappedData;
	uploadResource->Map(0, nullptr, &mappedData);
	memcpy(mappedData, data, size);
	uploadResource->Unmap(0, nullptr);

	// schedule buffer copy
	g_commandList->CopyResource(resource.Get(), uploadResource.Get());
}

template<typename T>
void CreateConstantBuffer(ComPtr<ID3D12Resource>& buffer, UINT8*& mappedData, const T& initialData)
{
	auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(T));

	g_device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&buffer)
	);

	buffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
	memcpy(mappedData, &initialData, sizeof(T));
}

bool LoadOBJModel(const std::string& filename) 
{
	std::vector<Mesh> loadedMeshes;
	std::string error;

	if (!OBJLoader::LoadOBJ(filename, loadedMeshes, error)) {
		MessageBoxA(nullptr, error.c_str(), "OBJ Load Error", MB_OK);
		return false;
	}

	g_commandAllocator->Reset();
	g_commandList->Reset(g_commandAllocator.Get(), nullptr);

	for (const auto& mesh : loadedMeshes) {
		RenderMesh renderMesh;

		// create vertex buffer
		CreateBuffer(mesh.vertices.data(),
			mesh.vertices.size() * sizeof(Vertex),
			renderMesh.vertexBuffer,
			D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

		// create index buffer
		CreateBuffer(mesh.indices.data(),
			mesh.indices.size() * sizeof(uint32_t),
			renderMesh.indexBuffer,
			D3D12_RESOURCE_STATE_INDEX_BUFFER);

		// create views
		renderMesh.vertexBufferView.BufferLocation = renderMesh.vertexBuffer->GetGPUVirtualAddress();
		renderMesh.vertexBufferView.StrideInBytes = sizeof(Vertex);
		renderMesh.vertexBufferView.SizeInBytes = mesh.vertices.size() * sizeof(Vertex);

		renderMesh.indexBufferView.BufferLocation = renderMesh.indexBuffer->GetGPUVirtualAddress();
		renderMesh.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
		renderMesh.indexBufferView.SizeInBytes = mesh.indices.size() * sizeof(uint32_t);

		renderMesh.indexCount = static_cast<UINT>(mesh.indices.size());

		g_meshes.push_back(renderMesh);
	}

	g_commandList->Close();
	ID3D12CommandList* ppCommandLists[] = { g_commandList.Get() };
	g_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	WaitForPreviousFrame();
	CleanupUploadResources();

	return true;
}

// setup directx objects
void InitD3D()
{
	UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	// create the device
	ComPtr<IDXGIFactory4> factory;
	CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));

	ComPtr<IDXGIAdapter1> hwAdapter;
	factory->EnumAdapters1(0, &hwAdapter); // get the first default adapter

	D3D12CreateDevice(hwAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device));

	// create command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue));

	// AFTER the queue create the swap chain
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Width = WindowWidth;
	swapChainDesc.Height = WindowHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChainLocal;
	factory->CreateSwapChainForHwnd(
		g_commandQueue.Get(),
		hWnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChainLocal
	);

	swapChainLocal.As(&g_swapChain);

	// create a descriptor heap for RTVs
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 2;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	g_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvHeap));
	g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = { };
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	g_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&g_dsvHeap));
	g_dsvHandle = g_dsvHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_RESOURCE_DESC depthDesc = {};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Alignment = 0;
	depthDesc.Width = WindowWidth;
	depthDesc.Height = WindowHeight;
	depthDesc.DepthOrArraySize = 1;
	depthDesc.MipLevels = 1;
	depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.SampleDesc.Quality = 0;
	depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
	depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
	depthOptimizedClearValue.DepthStencil.Stencil = 0;

	auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	g_device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthOptimizedClearValue,
		IID_PPV_ARGS(&g_depthBuffer)
	);

	// create depth stencil view
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	g_device->CreateDepthStencilView(g_depthBuffer.Get(), &dsvDesc, g_dsvHandle);

	D3D12_DESCRIPTOR_HEAP_DESC imGuiDesc = {};
	imGuiDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	imGuiDesc.NumDescriptors = 1; // one for the font texture
	imGuiDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	HRESULT hr = g_device->CreateDescriptorHeap(&imGuiDesc, IID_PPV_ARGS(&g_ImguiSrvDescHeap));

	if (FAILED(hr)) {
		MessageBox(nullptr, L"Failed to create imgui descriptor heap!", L"Error", MB_OK);
		exit(1);
	}

	D3D12_DESCRIPTOR_HEAP_DESC textureHeapDesc = {};
	textureHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	textureHeapDesc.NumDescriptors = 1;
	textureHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	hr = g_device->CreateDescriptorHeap(&textureHeapDesc, IID_PPV_ARGS(&g_textureSrvHeap));

	if (FAILED(hr)) {
		MessageBox(nullptr, L"Failed to create texture descriptor heap!", L"Error", MB_OK);
		exit(1);
	}
	g_textureHandle = g_textureSrvHeap->GetGPUDescriptorHandleForHeapStart();

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(g_rtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT n = 0; n < 2; n++)
	{
		g_swapChain->GetBuffer(n, IID_PPV_ARGS(&g_renderTargets[n]));
		g_device->CreateRenderTargetView(g_renderTargets[n].Get(), nullptr, rtvHandle);
		rtvHandle.Offset(1, g_rtvDescriptorSize);
	}

	// create command allocator and command list
	g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_commandAllocator));
	g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&g_commandList));

	// command lists are created in the recording state, close it for now and reset later
	g_commandList->Close();

	// create synchronization objects
	g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
	g_fenceValue = 1;
	g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr); // create a window event object

	if (g_fenceEvent == nullptr)
	{
		// failed to create event
	}

	CreatePipelineStateObject();
	CreateAssets();

	std::string absolutePath = "C:\\Users\\akyur\\Documents\\graphics-github\\dx12-sponza-renderer\\dx12-sponza-renderer\\models\\sponza.obj";

	if (!LoadOBJModel(absolutePath)) {
		MessageBox(nullptr, L"cannot load obj", L"Info", MB_OK);
	}

	XMMATRIX world = XMMatrixIdentity();
	DirectX::XMStoreFloat4x4(&g_worldMatrix, world);

	g_cameraPosition = { 0.0f, 5.0f, -15.0f };
	g_cameraTarget = { 0.0f, 0.0f, 0.0f };  // looking at the origin

	// create initial view matrix from your camera variables
	XMMATRIX view = XMMatrixLookAtLH(
		XMLoadFloat3(&g_cameraPosition),
		XMLoadFloat3(&g_cameraTarget),
		XMLoadFloat3(&g_cameraUp)
	);
	DirectX::XMStoreFloat4x4(&g_viewMatrix, view);

	XMMATRIX projection = XMMatrixPerspectiveFovLH(
		XM_PIDIV4,
		static_cast<float>(WindowWidth) / static_cast<float>(WindowHeight),
		0.1f, // near plane
		2000.0f // far plane
	);

	DirectX::XMStoreFloat4x4(&g_projectionMatrix, projection);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	// build font atlas manually first
	// dx12 backend will handle uploading this to a texture
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	ImGui_ImplWin32_Init(hWnd);

	// get the cpu and gpu descriptor handles from the heap
	D3D12_CPU_DESCRIPTOR_HANDLE fontCpuHandle = g_ImguiSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE fontGpuHandle = g_ImguiSrvDescHeap->GetGPUDescriptorHandleForHeapStart();

	ImGui_ImplDX12_Init(g_device.Get(), 2,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		g_ImguiSrvDescHeap.Get(),
		fontCpuHandle,
		fontGpuHandle
	);

	ImGui_ImplDX12_CreateDeviceObjects();
}

void PopulateCommandList()
{
	XMMATRIX world = XMMatrixIdentity();
	XMMATRIX view = XMLoadFloat4x4(&g_viewMatrix);
	XMMATRIX projection = XMLoadFloat4x4(&g_projectionMatrix);

	DirectX::XMStoreFloat4x4(&g_matrixBufferData.world, XMMatrixTranspose(world)); // hlsl expects column major
	DirectX::XMStoreFloat4x4(&g_matrixBufferData.view, XMMatrixTranspose(view));
	DirectX::XMStoreFloat4x4(&g_matrixBufferData.projection, XMMatrixTranspose(projection));

	memcpy(g_pMatrixConstantBufferStart, &g_matrixBufferData, sizeof(MatrixBuffer));

	XMMATRIX viewMatrix = XMLoadFloat4x4(&g_viewMatrix);
	XMMATRIX invViewMatrix = XMMatrixInverse(nullptr, viewMatrix);
	XMFLOAT4X4 invView;
	DirectX::XMStoreFloat4x4(&invView, invViewMatrix);

	g_lightBufferData.cameraPosition = g_cameraPosition;

	XMVECTOR lightDir = XMLoadFloat3(&g_lightBufferData.lightDirection);
	lightDir = XMVector3Normalize(lightDir);
	DirectX::XMStoreFloat3(&g_lightBufferData.lightDirection, lightDir);
	memcpy(g_pLightConstantBufferStart, &g_lightBufferData, sizeof(LightBuffer));

	// reset command allocator and command list
	g_commandAllocator->Reset();
	g_commandList->Reset(g_commandAllocator.Get(), g_pipelineState.Get()); // no pso yet so pass null

	// tell gpu that we will draw to it now by transitioning the back buffer from
	// present state to a render target state

	auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(
		g_renderTargets[g_currentBackBuffer].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);

	g_commandList->ResourceBarrier(1, &barrier2);

	// get the handle for the current back buffer manually and set it as the render target
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
		g_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		g_currentBackBuffer,
		g_rtvDescriptorSize
	);
	g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &g_dsvHandle);
	g_commandList->ClearDepthStencilView(g_dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	D3D12_VIEWPORT viewport = {};
	viewport.Width = static_cast<float>(WindowWidth);
	viewport.Height = static_cast<float>(WindowHeight);
	viewport.MaxDepth = 1.0f;
	g_commandList->RSSetViewports(1, &viewport);

	D3D12_RECT scissorRect = {};
	scissorRect.right = WindowWidth;
	scissorRect.bottom = WindowHeight;
	g_commandList->RSSetScissorRects(1, &scissorRect);

	// issue commands to clear the render target
	g_commandList->ClearRenderTargetView(rtvHandle, g_clearColor, 0, nullptr);

	ID3D12DescriptorHeap* mainHeaps[] = { g_textureSrvHeap.Get() };
	g_commandList->SetDescriptorHeaps(_countof(mainHeaps), mainHeaps);

	g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
	g_commandList->SetGraphicsRootConstantBufferView(0, g_matrixConstantBuffer->GetGPUVirtualAddress());
	g_commandList->SetGraphicsRootConstantBufferView(1, g_lightConstantBuffer->GetGPUVirtualAddress());
	g_commandList->SetGraphicsRootDescriptorTable(2, g_textureHandle);

	g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	for (const auto& mesh : g_meshes)
	{
		g_commandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);
		g_commandList->IASetIndexBuffer(&mesh.indexBufferView);
		g_commandList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
	}

	// set imgui descriptor heaps before rendering
	ID3D12DescriptorHeap* imGuiHeaps[] = { g_ImguiSrvDescHeap.Get() };
	g_commandList->SetDescriptorHeaps(_countof(imGuiHeaps), imGuiHeaps);

	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_commandList.Get());

	// transition the back buffer back to a present state
	auto barrier3 = CD3DX12_RESOURCE_BARRIER::Transition(
		g_renderTargets[g_currentBackBuffer].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);

	g_commandList->ResourceBarrier(1, &barrier3);
	g_commandList->Close();
}

void WaitForPreviousFrame()
{
	// signal the fence with the current value
	const UINT64 fence = g_fenceValue;
	g_commandQueue->Signal(g_fence.Get(), fence);
	g_fenceValue++;

	// wait for the previous frame if gpu is not done
	if (g_fence->GetCompletedValue() < fence)
	{
		g_fence->SetEventOnCompletion(fence, g_fenceEvent);
		WaitForSingleObject(g_fenceEvent, INFINITE);
	}

	// update the index of the current back buffer
	g_currentBackBuffer = g_swapChain->GetCurrentBackBufferIndex();
}

void LoadTexture()
{
	ComPtr<ID3D12CommandAllocator> tempCommandAllocator;
	ComPtr<ID3D12GraphicsCommandList> tempCommandList;

	g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tempCommandAllocator));
	g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tempCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&tempCommandList));

	int width, height, channels;
	unsigned char* imageData = stbi_load("mobo_atlas.png", &width, &height, &channels, 4);

	if (!imageData)
	{
		MessageBox(nullptr, L"Failed to load texture!", L"Error", MB_OK);
		return;
	}

	// create texture resource
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	g_device->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&g_texture)
	);

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(g_texture.Get(), 0, 1);
	auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

	g_device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&g_textureUploadHeap)
	);

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = imageData;
	textureData.RowPitch = width * 4;  // 4 bytes per pixel (RGBA)
	textureData.SlicePitch = textureData.RowPitch * height;
	UpdateSubresources(tempCommandList.Get(), g_texture.Get(), g_textureUploadHeap.Get(), 0, 0, 1, &textureData);

	// transition texture to shader resource state
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		g_texture.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);
	tempCommandList->ResourceBarrier(1, &barrier);

	tempCommandList->Close();
	ID3D12CommandList* ppCommandLists[] = { tempCommandList.Get() };
	g_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
	WaitForPreviousFrame();

	// create srv
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDesc.Format = textureDesc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvCpuHandle(g_textureSrvHeap->GetCPUDescriptorHandleForHeapStart());
	g_device->CreateShaderResourceView(g_texture.Get(), &srvDesc, srvCpuHandle);
	g_textureHandle = g_textureSrvHeap->GetGPUDescriptorHandleForHeapStart();

	stbi_image_free(imageData);
}

void CleanupUploadResources()
{
	// wait for GPU to finish all work
	WaitForPreviousFrame();
	WaitForPreviousFrame(); // double wait to be safe

	// now it's safe to clear upload resources
	g_uploadResources.clear();
}

void UpdateCamera(float deltaTime)
{
	// if deltaTime is too small, use a fixed value to prevent micro-movement
	if (deltaTime < 0.0001f) {
		deltaTime = 0.016f; // ~60 FPS
	}

	// camera movement
	XMVECTOR cameraPos = XMLoadFloat3(&g_cameraPosition);
	XMVECTOR cameraTarget = XMLoadFloat3(&g_cameraTarget);
	XMVECTOR cameraUp = XMLoadFloat3(&g_cameraUp);

	// calculate current camera direction
	XMVECTOR cameraDirection = XMVector3Normalize(cameraTarget - cameraPos);

	// calculate right vector (cross product of up and direction)
	XMVECTOR cameraRight = XMVector3Normalize(XMVector3Cross(cameraUp, cameraDirection));

	// movement based on input flags - use world-relative movement
	float moveSpeed = g_cameraMoveSpeed * deltaTime;

	if (g_cameraInput.moveForward) {
		cameraPos += cameraDirection * moveSpeed;
		cameraTarget += cameraDirection * moveSpeed;
	}
	if (g_cameraInput.moveBackward) {
		cameraPos -= cameraDirection * moveSpeed;
		cameraTarget -= cameraDirection * moveSpeed;
	}
	if (g_cameraInput.moveLeft) {
		cameraPos -= cameraRight * moveSpeed;
		cameraTarget -= cameraRight * moveSpeed;
	}
	if (g_cameraInput.moveRight) {
		cameraPos += cameraRight * moveSpeed;
		cameraTarget += cameraRight * moveSpeed;
	}
	if (g_cameraInput.moveUp) {
		cameraPos += cameraUp * moveSpeed;
		cameraTarget += cameraUp * moveSpeed;
	}
	if (g_cameraInput.moveDown) {
		cameraPos -= cameraUp * moveSpeed;
		cameraTarget -= cameraUp * moveSpeed;
	}

	// rotation - use proper free-look rotation
	float rotationSpeed = g_cameraRotationSpeed * deltaTime;

	if (g_cameraInput.rotateLeft || g_cameraInput.rotateRight ||
		g_cameraInput.rotateUp || g_cameraInput.rotateDown)
	{
		// recalculate direction vectors
		cameraDirection = XMVector3Normalize(cameraTarget - cameraPos);
		cameraRight = XMVector3Normalize(XMVector3Cross(cameraUp, cameraDirection));

		// yaw rotation (left/right)
		if (g_cameraInput.rotateLeft || g_cameraInput.rotateRight) {
			XMMATRIX yawRotation = XMMatrixRotationAxis(cameraUp,
				g_cameraInput.rotateLeft ? rotationSpeed : -rotationSpeed);
			cameraDirection = XMVector3TransformNormal(cameraDirection, yawRotation);
		}

		// pitch rotation (up/down) - limit to avoid flipping
		if (g_cameraInput.rotateUp || g_cameraInput.rotateDown) {
			XMMATRIX pitchRotation = XMMatrixRotationAxis(cameraRight,
				g_cameraInput.rotateUp ? rotationSpeed : -rotationSpeed);

			// apply pitch rotation
			XMVECTOR newDirection = XMVector3TransformNormal(cameraDirection, pitchRotation);

			// check if we're not looking straight up or down (avoid gimbal lock)
			float newPitch = asin(-XMVectorGetY(newDirection));
			if (fabs(newPitch) < XM_PIDIV2 - 0.1f) { // limit to ~80 degrees
				cameraDirection = newDirection;
			}
		}

		// update target position based on new direction
		cameraTarget = cameraPos + cameraDirection;
	}

	// store updated values
	XMFLOAT3 newPosition, newTarget;
	DirectX::XMStoreFloat3(&newPosition, cameraPos);
	DirectX::XMStoreFloat3(&newTarget, cameraTarget);

	// check for valid values before updating
	if (!isnan(newPosition.x) && !isinf(newPosition.x) &&
		!isnan(newPosition.y) && !isinf(newPosition.y) &&
		!isnan(newPosition.z) && !isinf(newPosition.z)) {
		g_cameraPosition = newPosition;
		g_cameraTarget = newTarget;
	}
	else {
		// reset to safe values if invalid
		g_cameraPosition = { 0.0f, 5.0f, -15.0f };
		g_cameraTarget = { 0.0f, 0.0f, 0.0f };
	}

	// update view matrix
	XMMATRIX view = XMMatrixLookAtLH(
		XMLoadFloat3(&g_cameraPosition),
		XMLoadFloat3(&g_cameraTarget),
		XMLoadFloat3(&g_cameraUp)
	);
	DirectX::XMStoreFloat4x4(&g_viewMatrix, view);
}