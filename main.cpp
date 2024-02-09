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
namespace offsets {
    const auto LocalPlayer = 0x17E0A8;
    const auto ViewMatrix = 0x17DFD0;
    const auto EntityList = 0x18AC04;
    const auto AmountOfPlayers = 0x18AC0C;
}



extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern ID3D11RenderTargetView* render_target_view;

const auto memory = Memory("ac_client.exe");
const auto client = memory.GetModuleAddress("ac_client.exe");

std::atomic<bool> running = true;

void UpdateThread() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

struct ViewMatrix {
    float matrix[16];

    float operator[](int i) const {
        return matrix[i];
    }
};
struct Vec4 {
    float x, y, z, w;
};
struct Vec3 {
    float x, y, z;
};
struct Vec2 {
    float x, y;
};

Vec4 clipCoords;
Vec3 xyzpos;
Vec2 vScreen;

Vec3 getEntityPosition(int entitylistOffset, int i) {
    const auto localPlayer = memory.Read<std::int32_t>(client + offsets::LocalPlayer);
    const auto entityList = memory.Read<std::int32_t>(client + offsets::EntityList);
    const auto entity = memory.Read<std::int32_t>(entityList + 4 * i);

    const auto x = memory.Read<float>(entity + 0x28);
    const auto y = memory.Read<float>(entity + 0x30);
    const auto z = memory.Read<float>(entity + 0x2C);
    ImGui::Text("Client Address: 0x%X", client);
    ImGui::Text("EntityList Address: 0x%X", entityList);
    ImGui::Text("Entity Address: 0x%X", entity);
    ImGui::Text("x: %f", x);
    ImGui::Text("y: %f", y);
    ImGui::Text("z: %f", z);

    return { x, y, z };
}
XMMATRIX Viewmatrix;
const auto HEIGHT = 1920;
const auto WIDTH = 1080;
bool WorldToScreen(Vec3 world, Vec2& screen) {
    XMFLOAT4X4 matView;
    XMStoreFloat4x4(&matView, Viewmatrix);
    XMVECTOR worldPos = XMVectorSet(world.x, world.y, world.z, 1.0f);
    XMVECTOR screenSpace = XMVector4Transform(worldPos, Viewmatrix);


    screenSpace = XMVectorDivide(screenSpace, XMVectorSplatW(screenSpace));

    const float epsilon = (WIDTH > HEIGHT) ?
        (WIDTH * 0.00001f) :
        (HEIGHT * 0.00001f);

    if (XMVectorGetW(screenSpace) < epsilon)
    {
        screen.x = WIDTH * 100000;
        screen.y = HEIGHT * 100000;

        return false;
    }

    screen.x = (WIDTH / 2.0f) + (XMVectorGetX(screenSpace) * WIDTH) / 2.0f;
    screen.y = (HEIGHT / 2.0f) - (XMVectorGetY(screenSpace) * HEIGHT) / 2.0f;
    ImGui::Text("Raw Position: x=%f, y=%f, z=%f", world.x, world.y, world.z);
    return true;
}

void MainCode();

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

    std::thread updateThread(UpdateThread);

    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                running = false;
            }
        }
        if (!running) {
            break;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();

        ImGui::NewFrame();
        ImGui::Begin("Overlay Window");
        MainCode();

        ImGui::End();

        ImGui::Render();
        constexpr float color[4]{ 0.f, 0.f, 0.f, 0.f };
        device_context->OMSetRenderTargets(1U, &render_target_view, nullptr);
        device_context->ClearRenderTargetView(render_target_view, color);

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        swap_chain->Present(1U, 0U);
    }

    updateThread.join();

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

void MainCode() {
    const auto aop = memory.Read<int>(client + offsets::AmountOfPlayers);
    XMMATRIX Viewmatrix = memory.Read<XMMATRIX>(offsets::ViewMatrix);


    for (int a = 1; a <= aop; a++) {
        xyzpos = getEntityPosition(offsets::EntityList, a);

        vScreen = { 0, 0 };
        if (!WorldToScreen(xyzpos, vScreen))
            continue;

        // need to draw here


        ImGui::Text("Player %d:", a);
        ImGui::Text("clipCoords.w: %f", clipCoords.w);
        ImGui::Text("Screen.x: %f", vScreen.x);
        ImGui::Text("Screen.y: %f", vScreen.y);
    }
}
