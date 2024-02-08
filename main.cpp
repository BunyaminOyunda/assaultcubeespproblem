#include "memory.h"
#include <iostream>
#include <dwmapi.h>
#include <d3d11.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>
#include <thread>
#include <atomic>

namespace offsets {
    const auto LocalPlayer = 0x17E0A8;
    const auto ViewMatrix = 0x17DFD0;
    const auto EntityList = 0x18AC04;
    const auto AmountOfPlayers = 0x18AC0C;
}



extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern ID3D11RenderTargetView* render_target_view;

int windowWidth;
int windowHeight;

std::atomic<bool> running = true;

void UpdateThread() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
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

Vec4 clipCoords;
Vec3 xyzpos;
Vec2 screen;
float matrix[16] = { 0.0 };

Vec3 getEntityPosition(int entitylistOffset, int i) {

    const auto memory = Memory("ac_client.exe");
    const auto client = memory.GetModuleAddress("ac_client.exe");

    const auto localPlayer = memory.Read<std::int32_t>(client + offsets::LocalPlayer);
    const auto entityList = memory.Read<std::int32_t>(client + offsets::EntityList);
    const auto entity = memory.Read<std::int32_t>(entityList + 4 * i);

    const auto x = memory.Read<float>(entity + 0x4);
    const auto y = memory.Read<float>(entity + 0x30);
    const auto z = memory.Read<float>(entity + 0x8);
    ImGui::Text("Client Address: 0x%X", client);
    ImGui::Text("EntityList Address: 0x%X", entityList);
    ImGui::Text("Entity Address: 0x%X", entity);
    ImGui::Text("x: %f", x);
    ImGui::Text("y: %f", y);
    ImGui::Text("z: %f", z);

    return { x, y, z };
}

float* initializeMatrix(int matrixOffset) {
    const auto memory = Memory("ac_client.exe");
    const auto client = memory.GetModuleAddress("ac_client.exe");

    float* matrix = new float[16];

    
    for (int i = 0; i < 16; i++) {
        matrix[i] = memory.Read<float>(client + matrixOffset + i * sizeof(float));
    }
    return matrix;
}

bool WorldToScreen(Vec3 pos, Vec2& screen, float matrix[16], int windowWidth, int windowHeight) {
    clipCoords.x = pos.x * matrix[0] + pos.y * matrix[4] + pos.z * matrix[8] + matrix[12];
    clipCoords.y = pos.x * matrix[1] + pos.y * matrix[5] + pos.z * matrix[9] + matrix[13];
    clipCoords.z = pos.x * matrix[2] + pos.y * matrix[6] + pos.z * matrix[10] + matrix[14];
    clipCoords.w = pos.x * matrix[3] + pos.y * matrix[7] + pos.z * matrix[11] + matrix[15];

    if (clipCoords.w < 0.1f) {
        return false;
    }

    ImGui::SetWindowSize(ImVec2(800, 600));
    Vec3 NDC;
    NDC.x = clipCoords.x / clipCoords.w;
    NDC.y = clipCoords.y / clipCoords.w;
    NDC.z = clipCoords.z / clipCoords.w;

    ImGui::Text("Raw Position: x=%f, y=%f, z=%f", pos.x, pos.y, pos.z);
    ImGui::Text("Matrix:");
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            ImGui::Text("[%d][%d]: %f", i, j, matrix[i * 4 + j]);
        }
    }
    ImGui::Text("ClipCoords: x=%f, y=%f, z=%f, w=%f", clipCoords.x, clipCoords.y, clipCoords.z, clipCoords.w);
    ImGui::Text("NDC.x: %f, NDC.y: %f, NDC.z: %f", NDC.x, NDC.y, NDC.z);
    screen.x = (windowWidth / 2 * NDC.x) + (NDC.x + windowWidth / 2);
    screen.y = -(windowHeight / 2 * NDC.y) + (NDC.y + windowHeight / 2);

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
    const auto memory = Memory("ac_client.exe");
    const auto client = memory.GetModuleAddress("ac_client.exe");
    const auto aop = memory.Read<int>(client + offsets::AmountOfPlayers);

    for (int a = 1; a <= aop; a++) {
        xyzpos = getEntityPosition(offsets::EntityList, a);

        float* matrix = initializeMatrix(offsets::ViewMatrix);

        screen = { 0, 0 };
        if (WorldToScreen(xyzpos, screen, matrix, 1920, 1080)) {

            ImVec2 rectMin = ImVec2(screen.x - 10.0f, screen.y - 10.0f);
            ImVec2 rectMax = ImVec2(screen.x + 10.0f, screen.y + 10.0f);
            ImGui::GetBackgroundDrawList()->AddRectFilled(rectMin, rectMax, ImColor(1.f, 0.f, 0.f));

            ImGui::Text("Player %d:", a);
            ImGui::Text("clipCoords.w: %f", clipCoords.w);
            ImGui::Text("Screen.X: %f", screen.x);
            ImGui::Text("Screen.Y: %f", screen.y);
        }
        delete[] matrix;
    }
}
