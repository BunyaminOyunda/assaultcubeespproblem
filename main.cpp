#include "memory.h"
#include <iostream>
#include <dwmapi.h>
#include <d3d11.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>
#include <thread>
#include <atomic>
#include <directxmath.h>


using namespace process;

using namespace DirectX;


#define WIDTH  1920
#define HEIGHT 1080
HWND hWnd = FindWindow(NULL, "AssaultCube");
HANDLE process_id = process::get_process_id("ac_client.exe");


static ID3D11Device* device = nullptr;
static IDXGISwapChain* swap_chain = nullptr;
static ID3D11DeviceContext* device_context = nullptr;
static ID3D11RenderTargetView* render_target_view = nullptr;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct Vec4 {
    float x, y, z, w;
};
struct Vec3 {
    float x, y, z;
};
struct Vec2 {
    float x, y;
};

namespace offsets
{
    constexpr auto LocalPlayer = 0x0058AC00;
    constexpr auto ViewMatrix = 0x0057DFD0;
    constexpr auto EntityList = 0x0058AC04;
    constexpr auto AmountOfPlayers = 0x0058AC0C;

    constexpr auto Position = 0x28;
    constexpr auto Team = 0x30C;
    constexpr auto Health = 0xEC;
}

void draw_line(int x, int y, int x1, int y1, ImColor color)
{
    ImGui::GetBackgroundDrawList()->AddLine(ImVec2(static_cast<float>(x), static_cast<float>(y)), ImVec2(static_cast<float>(x1), static_cast<float>(y1)), color);
}
bool WorldToScreen(Vec3 world, Vec2& screen)
{
    XMMATRIX Viewmatrix = process::read<XMMATRIX>(offsets::ViewMatrix);

    XMFLOAT4X4 matView;
    XMStoreFloat4x4(&matView, Viewmatrix);

    XMVECTOR worldPos = XMVectorSet(world.x, world.y, world.z, 1.0f);
    XMVECTOR screenSpace = XMVector4Transform(worldPos, Viewmatrix);

    const float epsilon = (WIDTH > HEIGHT) ?
        (WIDTH * 0.00001f) :
        (HEIGHT * 0.00001f);

    if (XMVectorGetW(screenSpace) < epsilon)
    {
        screen.x = WIDTH * 100000;
        screen.y = HEIGHT * 100000;

        return false;
    }

    screenSpace = XMVectorDivide(screenSpace, XMVectorSplatW(screenSpace));

    screen.x = (WIDTH / 2.0f) + (XMVectorGetX(screenSpace) * WIDTH) / 2.0f;
    screen.y = (HEIGHT / 2.0f) - (XMVectorGetY(screenSpace) * HEIGHT) / 2.0f;

    return true;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Creating window
    HANDLE process_id = process::get_process_id("ac_client.exe");

    WNDCLASSEX windowclass = { 0 };
    ZeroMemory(&windowclass, sizeof(WNDCLASSEX));

    windowclass.cbClsExtra = NULL;
    windowclass.cbWndExtra = NULL;
    windowclass.cbSize = sizeof(WNDCLASSEX);
    windowclass.style = CS_HREDRAW | CS_VREDRAW;
    windowclass.lpfnWndProc = WindowProc;
    windowclass.hInstance = NULL;
    windowclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowclass.hIcon = LoadIcon(0, IDI_APPLICATION);
    windowclass.hIconSm = LoadIcon(0, IDI_APPLICATION);
    windowclass.hbrBackground = (HBRUSH)RGB(0, 0, 0);
    windowclass.lpszClassName = "Overlay";
    windowclass.lpszMenuName = "Overlay";

    if (!RegisterClassEx(&windowclass)) {
        MessageBox(NULL, "RegisterClassEx() failed", "Error", MB_OK);
        return false;
    }

    HWND window = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT, "Overlay", "Overlay",
        WS_POPUP, 0, 0, WIDTH, HEIGHT, NULL, NULL, windowclass.hInstance, NULL);

    SetLayeredWindowAttributes(window, RGB(0, 0, 0), 255, LWA_ALPHA);

    RECT client_area = {};
    GetClientRect(window, &client_area);

    RECT window_area = {};
    GetWindowRect(window, &window_area);

    POINT diff = {};
    ClientToScreen(window, &diff);

    const MARGINS margins = {
        window_area.left + (diff.x - window_area.left),
        window_area.top + (diff.y - window_area.top),

        client_area.right,
        client_area.bottom
    };

    DwmExtendFrameIntoClientArea(window, &margins);

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    ZeroMemory(&swapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC));

    swapChainDesc.BufferCount = 1;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.Width = WIDTH;
    swapChainDesc.BufferDesc.Height = HEIGHT;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hWnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;

    D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, NULL, NULL, NULL, D3D11_SDK_VERSION, &swapChainDesc, &swap_chain, &device, NULL, &device_context);

    ID3D11Texture2D* pBackBuffer;
    swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);

    device->CreateRenderTargetView(pBackBuffer, NULL, &render_target_view);
    pBackBuffer->Release();
    ShowWindow(window, nCmdShow);
    UpdateWindow(window);

    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(device, device_context);

    MSG message;
    while (!GetAsyncKeyState(VK_END))
    {
        while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);

            if (message.message == WM_QUIT) {
                return message.wParam;
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();

        ImGui::NewFrame();

        uintptr_t localplayer = process::read<uintptr_t >(offsets::LocalPlayer);
        uintptr_t localplayer_team = process::read<uintptr_t>(localplayer + offsets::Team);

        uintptr_t entitylist = process::read<uintptr_t>(offsets::EntityList);
        uintptr_t maxentities = process::read<uintptr_t>(offsets::AmountOfPlayers);
        Vec2 vScreen;
        for (size_t i = 0; i < maxentities; i++)
        {
            uintptr_t enemy = process::read<uintptr_t>(entitylist + (i * 0x4));
            uintptr_t enemy_team = process::read<uintptr_t>(enemy + offsets::Team);
            uintptr_t enemy_health = process::read<uintptr_t>(enemy + offsets::Health);

            Vec3 enemy_pos = process::read<Vec3>(enemy + offsets::Position);

            if (!enemy || enemy_team == localplayer_team || enemy_health <= 0)
                continue;

            if (!WorldToScreen(enemy_pos, vScreen))
                continue;

            draw_line(WIDTH / 2, HEIGHT, vScreen.x, vScreen.y, ImColor(255, 255, 255));
        }
        ImGui::Render();

        float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        device_context->OMSetRenderTargets(1, &render_target_view, nullptr);
        device_context->ClearRenderTargetView(render_target_view, clear);

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swap_chain->Present(1, 0);

        Sleep(10);
    }
}

