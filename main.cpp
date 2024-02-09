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
using namespace DirectX;


// Version 1.2.0.2
//namespace offsets {
//    const auto LocalPlayer = 0x10F4F4;
//    const auto EntityList = 0x10F4F8;
//    const auto AmountOfPlayers = 0x10F500;
//    const auto ViewMatrix = 0x101AE8;
//}

// Version 1.3.0.0 (offsets from a trainer on github)
//namespace offsets {
//    const auto LocalPlayer = 0x17B0B8;
//    const auto ViewMatrix = 0x17AFE0;
//    const auto EntityList = 0x187C10;
//    const auto AmountOfPlayers = 0x187C18;
//}

// Version 1.3.0.2
namespace offsets
{
    constexpr auto LocalPlayer = 0x17E0A8;
    constexpr auto ViewMatrix = 0x17DFD0;
    constexpr auto EntityList = 0x18AC04;
    constexpr auto AmountOfPlayers = 0x18AC0C;

    constexpr auto Position = 0x28;
    constexpr auto Team = 0x30C;
    constexpr auto Health = 0xEC;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern ID3D11RenderTargetView* render_target_view;

const auto memory = Memory("ac_client.exe");
const auto client = memory.GetModuleAddress("ac_client.exe");

void draw_line(int x, int y, int x1, int y1, ImColor color)
{
    ImGui::GetBackgroundDrawList()->AddLine(ImVec2(static_cast<float>(x), static_cast<float>(y)), ImVec2(static_cast<float>(x1), static_cast<float>(y1)), color);
}

struct Vec4 {
    float x, y, z, w;
};
struct Vec3 {
    float x, y, z;
};
struct Vec2 {
    float x, y;
};

const auto HEIGHT = 1920;
const auto WIDTH = 1080;

bool WorldToScreen(Vec3 world, Vec2& screen)
{
    XMMATRIX Viewmatrix = memory.Read<XMMATRIX>(client + offsets::ViewMatrix);

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


LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param)) {
        return 0L;
    }

    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0L;
    }
    return DefWindowProc(window, message, w_param, l_param);
}

INT APIENTRY WinMain(HINSTANCE instance, HINSTANCE, PSTR, INT cmd_show) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_procedure;
    wc.hInstance = instance;
    wc.lpszClassName = L"External overlay Class";

    RegisterClassExW(&wc);

    const HWND window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        wc.lpszClassName,
        L"External Overlay",
        WS_POPUP,
        0,
        0,
        1920,
        1080,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );

    SetLayeredWindowAttributes(window, RGB(0, 0, 0), BYTE(255), LWA_ALPHA);
    {
        RECT client_area{};
        GetClientRect(window, &client_area);

        RECT window_area{};
        GetWindowRect(window, &window_area);

        POINT diff{};
        ClientToScreen(window, &diff);

        const MARGINS margins{
            window_area.left + (diff.x - window_area.left),
            window_area.top + (diff.y - window_area.top),
            client_area.right,
            client_area.bottom
        };

        DwmExtendFrameIntoClientArea(window, &margins);
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.RefreshRate.Numerator = 60U;
    sd.BufferDesc.RefreshRate.Denominator = 1U;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1U;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2U;
    sd.OutputWindow = window;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    constexpr D3D_FEATURE_LEVEL levels[2]{
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };
    ID3D11Device* device{ nullptr };
    ID3D11DeviceContext* device_context{ nullptr };
    IDXGISwapChain* swap_chain{ nullptr };
    ID3D11RenderTargetView* render_target_view{ nullptr };
    D3D_FEATURE_LEVEL level{};

    D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0U,
        levels,
        2U,
        D3D11_SDK_VERSION,
        &sd,
        &swap_chain,
        &device,
        &level,
        &device_context
    );

    ID3D11Texture2D* back_buffer{ nullptr };
    swap_chain->GetBuffer(0U, IID_PPV_ARGS(&back_buffer));

    if (back_buffer) {
        device->CreateRenderTargetView(back_buffer, nullptr, &render_target_view);
        back_buffer->Release();
    }
    else {
        return 1;
    }

    ShowWindow(window, cmd_show);
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

        // ESP here
        uintptr_t localplayer = memory.Read<uintptr_t>(client + offsets::LocalPlayer);
        uintptr_t localplayer_team = memory.Read<uintptr_t>(localplayer + offsets::Team);

        uintptr_t entitylist = memory.Read<uintptr_t>(client + offsets::EntityList);
        uintptr_t maxentities = memory.Read<uintptr_t>(client + offsets::AmountOfPlayers);

        Vec2 vScreen;
        for (size_t i = 0; i < maxentities; i++)
        {
            uintptr_t enemy = memory.Read<uintptr_t>(entitylist + (i * 0x4));
            uintptr_t enemy_team = memory.Read<uintptr_t>(enemy + offsets::Team);
            uintptr_t enemy_health = memory.Read<uintptr_t>(enemy + offsets::Health);

            Vec3 enemy_pos = memory.Read<Vec3>(enemy + offsets::Position);

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

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();

    ImGui::DestroyContext();

    if (swap_chain) {
        swap_chain->Release();
    }
    if (device_context) {
        device_context->Release();
    }
    if (device) {
        device->Release();
    }
    if (render_target_view) {
        render_target_view->Release();
    }
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}
