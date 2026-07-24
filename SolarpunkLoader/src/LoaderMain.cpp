#include "Injector.h"
#include "LocalCompatibility.h"
#include "resource.h"

#include <Windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dwmapi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iterator>
#include <string>

extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);

namespace {

    constexpr wchar_t WindowClassName[] =
        L"SolarpunkTrainerLoaderWindow";
    constexpr float LogicalWindowWidth = 520.0f;
    constexpr float LogicalWindowHeight = 308.0f;
    constexpr ULONGLONG ProcessPollMilliseconds = 700;

    ID3D11Device* gDevice = nullptr;
    ID3D11DeviceContext* gDeviceContext = nullptr;
    IDXGISwapChain* gSwapChain = nullptr;
    ID3D11RenderTargetView* gRenderTarget = nullptr;

    ImFont* gTitleFont = nullptr;
    ImFont* gBodyFont = nullptr;
    ImFont* gSmallFont = nullptr;

    enum class ViewState {
        Waiting,
        Analyzing,
        Ready,
        Loading,
        Loaded,
        Error
    };

    struct AppState {
        DWORD ProcessId = 0;
        DWORD AnalysisProcessId = 0;
        std::filesystem::path TrainerPath;
        std::filesystem::path SchemaProbePath;
        bool TrainerAvailable = false;
        bool SchemaProbeAvailable = false;
        ViewState View = ViewState::Waiting;
        std::string Detail =
            "Start Solarpunk to make the trainer available.";
        ULONGLONG NextProcessPoll = 0;
        std::future<Loader::CompatibilityResult> Compatibility;
        std::future<Loader::InjectionResult> Injection;
    };

    ImVec4 Mix(
        const ImVec4& from,
        const ImVec4& to,
        float amount) {
        amount = std::clamp(amount, 0.0f, 1.0f);
        return ImVec4(
            from.x + (to.x - from.x) * amount,
            from.y + (to.y - from.y) * amount,
            from.z + (to.z - from.z) * amount,
            from.w + (to.w - from.w) * amount);
    }

    ImU32 Color(const ImVec4& value) {
        return ImGui::ColorConvertFloat4ToU32(value);
    }

    void CreateRenderTarget() {
        ID3D11Texture2D* backBuffer = nullptr;
        if (SUCCEEDED(gSwapChain->GetBuffer(
            0,
            IID_PPV_ARGS(&backBuffer)))) {
            gDevice->CreateRenderTargetView(
                backBuffer,
                nullptr,
                &gRenderTarget);
            backBuffer->Release();
        }
    }

    void CleanupRenderTarget() {
        if (gRenderTarget) {
            gRenderTarget->Release();
            gRenderTarget = nullptr;
        }
    }

    bool CreateDeviceD3D(HWND window) {
        DXGI_SWAP_CHAIN_DESC swapChain{};
        swapChain.BufferCount = 2;
        swapChain.BufferDesc.Format =
            DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChain.BufferUsage =
            DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChain.OutputWindow = window;
        swapChain.SampleDesc.Count = 1;
        swapChain.Windowed = TRUE;
        swapChain.SwapEffect =
            DXGI_SWAP_EFFECT_DISCARD;

        constexpr D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL featureLevel{};
        const HRESULT result =
            D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                0,
                featureLevels,
                static_cast<UINT>(
                    std::size(featureLevels)),
                D3D11_SDK_VERSION,
                &swapChain,
                &gSwapChain,
                &gDevice,
                &featureLevel,
                &gDeviceContext);
        if (FAILED(result))
            return false;
        CreateRenderTarget();
        return gRenderTarget != nullptr;
    }

    void CleanupDeviceD3D() {
        CleanupRenderTarget();
        if (gSwapChain) {
            gSwapChain->Release();
            gSwapChain = nullptr;
        }
        if (gDeviceContext) {
            gDeviceContext->Release();
            gDeviceContext = nullptr;
        }
        if (gDevice) {
            gDevice->Release();
            gDevice = nullptr;
        }
    }

    bool LoadFonts(float scale) {
        HRSRC resource = FindResourceW(
            nullptr,
            MAKEINTRESOURCEW(IDR_FONT_INTER),
            RT_RCDATA);
        if (!resource)
            return false;
        HGLOBAL loaded = LoadResource(nullptr, resource);
        const DWORD size = SizeofResource(nullptr, resource);
        void* data = loaded ? LockResource(loaded) : nullptr;
        if (!data || !size)
            return false;

        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig config{};
        config.FontDataOwnedByAtlas = false;
        config.OversampleH = 2;
        config.OversampleV = 2;
        config.RasterizerDensity = 1.0f;

        gTitleFont = io.Fonts->AddFontFromMemoryTTF(
            data,
            static_cast<int>(size),
            21.0f * scale,
            &config);
        gBodyFont = io.Fonts->AddFontFromMemoryTTF(
            data,
            static_cast<int>(size),
            15.0f * scale,
            &config);
        gSmallFont = io.Fonts->AddFontFromMemoryTTF(
            data,
            static_cast<int>(size),
            12.5f * scale,
            &config);
        io.FontDefault = gBodyFont;
        return gTitleFont && gBodyFont && gSmallFont;
    }

    void ApplyTheme(float scale) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(0.0f, 0.0f);
        style.WindowRounding = 14.0f * scale;
        style.WindowBorderSize = 1.0f * scale;
        style.FrameRounding = 8.0f * scale;
        style.FrameBorderSize = 0.0f;
        style.ItemSpacing =
            ImVec2(10.0f * scale, 10.0f * scale);
        style.Colors[ImGuiCol_WindowBg] =
            ImVec4(0.035f, 0.039f, 0.043f, 1.0f);
        style.Colors[ImGuiCol_Border] =
            ImVec4(0.16f, 0.18f, 0.20f, 1.0f);
        style.Colors[ImGuiCol_Text] =
            ImVec4(0.92f, 0.93f, 0.94f, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] =
            ImVec4(0.42f, 0.45f, 0.48f, 1.0f);
        style.Colors[ImGuiCol_NavHighlight] =
            ImVec4(0.27f, 0.85f, 0.58f, 0.9f);
    }

    bool PrimaryButton(
        const char* id,
        const char* label,
        const ImVec2& size,
        bool enabled) {
        static float response = 0.0f;

        if (!enabled)
            ImGui::BeginDisabled();
        const bool pressed =
            ImGui::InvisibleButton(id, size);
        const bool hovered =
            enabled && ImGui::IsItemHovered();
        const bool active =
            enabled && ImGui::IsItemActive();
        const bool focused =
            ImGui::IsItemFocused();
        if (!enabled)
            ImGui::EndDisabled();

        const float target = active
            ? 1.0f
            : hovered
                ? 0.64f
                : 0.0f;
        const float speed = 1.0f
            - std::exp(
                -ImGui::GetIO().DeltaTime * 15.0f);
        response += (target - response) * speed;

        const ImVec4 disabled(
            0.12f, 0.13f, 0.14f, 1.0f);
        const ImVec4 base(
            0.17f, 0.68f, 0.43f, 1.0f);
        const ImVec4 reactive(
            0.25f, 0.82f, 0.54f, 1.0f);
        const ImVec4 fill = enabled
            ? Mix(base, reactive, response)
            : disabled;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        const float rounding = 8.0f
            * ImGui::GetIO().FontGlobalScale;
        draw->AddRectFilled(
            minimum,
            maximum,
            Color(fill),
            rounding);
        if (focused) {
            draw->AddRect(
                ImVec2(minimum.x - 2.0f, minimum.y - 2.0f),
                ImVec2(maximum.x + 2.0f, maximum.y + 2.0f),
                Color(ImVec4(
                    0.43f, 0.95f, 0.69f, 0.9f)),
                rounding + 2.0f,
                0,
                1.0f);
        }

        ImGui::PushFont(gBodyFont);
        const ImVec2 textSize =
            ImGui::CalcTextSize(label);
        draw->AddText(
            ImVec2(
                minimum.x
                    + (size.x - textSize.x) * 0.5f,
                minimum.y
                    + (size.y - textSize.y) * 0.5f),
            Color(enabled
                ? ImVec4(0.025f, 0.055f, 0.04f, 1.0f)
                : ImVec4(0.43f, 0.46f, 0.48f, 1.0f)),
            label);
        ImGui::PopFont();
        return enabled && pressed;
    }

    void BeginCompatibility(AppState& app) {
        const DWORD processId = app.ProcessId;
        const std::filesystem::path schemaProbe =
            app.SchemaProbePath;
        app.AnalysisProcessId = processId;
        app.View = ViewState::Analyzing;
        app.Detail =
            "Fingerprinting this build and checking the local schema cache...";
        app.Compatibility = std::async(
            std::launch::async,
            [processId, schemaProbe]() {
                return Loader::EnsureLocalCompatibility(
                    processId,
                    schemaProbe);
            });
    }

    void RefreshState(AppState& app) {
        if (app.Compatibility.valid()
            && app.Compatibility.wait_for(
                std::chrono::seconds(0))
                == std::future_status::ready) {
            const Loader::CompatibilityResult result =
                app.Compatibility.get();
            if (app.ProcessId == app.AnalysisProcessId) {
                app.View = result.Success
                    ? ViewState::Ready
                    : ViewState::Error;
                app.Detail = result.Message;
                app.NextProcessPoll =
                    GetTickCount64()
                    + ProcessPollMilliseconds;
            }
        }

        if (app.Injection.valid()
            && app.Injection.wait_for(
                std::chrono::seconds(0))
                == std::future_status::ready) {
            const Loader::InjectionResult result =
                app.Injection.get();
            app.View = result.Success
                ? ViewState::Loaded
                : ViewState::Error;
            app.Detail = result.Message;
            app.NextProcessPoll =
                GetTickCount64()
                + ProcessPollMilliseconds;
        }

        const ULONGLONG now = GetTickCount64();
        if (app.View == ViewState::Loading
            || app.View == ViewState::Analyzing
            || now < app.NextProcessPoll) {
            return;
        }
        app.NextProcessPoll =
            now + ProcessPollMilliseconds;

        const DWORD observedProcess =
            Loader::FindSolarpunkProcess();
        const bool processChanged =
            observedProcess != app.ProcessId;
        app.ProcessId = observedProcess;
        app.TrainerPath = Loader::ResolveTrainerPath();
        app.SchemaProbePath =
            Loader::ResolveSchemaProbePath();
        std::error_code pathError;
        app.TrainerAvailable =
            std::filesystem::is_regular_file(
                app.TrainerPath,
                pathError);
        pathError.clear();
        app.SchemaProbeAvailable =
            std::filesystem::is_regular_file(
                app.SchemaProbePath,
                pathError);

        if (!app.ProcessId) {
            app.AnalysisProcessId = 0;
            app.View = ViewState::Waiting;
            app.Detail =
                "Start Solarpunk to make the trainer available.";
            return;
        }
        if (!app.TrainerAvailable) {
            app.View = ViewState::Error;
            app.Detail =
                "Place SolarpunkTrainer.dll beside this loader.";
            return;
        }
        if (!app.SchemaProbeAvailable) {
            app.View = ViewState::Error;
            app.Detail =
                "SolarpunkSchemaProbe.dll is missing beside this loader.";
            return;
        }
        if (Loader::IsTrainerLoaded(
            app.ProcessId,
            app.TrainerPath)) {
            app.View = ViewState::Loaded;
            app.Detail =
                "Trainer is active in the current game session.";
            return;
        }
        if (processChanged) {
            BeginCompatibility(app);
        }
    }

    void BeginInjection(AppState& app) {
        const DWORD processId = app.ProcessId;
        const std::filesystem::path trainer =
            app.TrainerPath;
        app.View = ViewState::Loading;
        app.Detail =
            "Loading the trainer into Solarpunk...";
        app.Injection = std::async(
            std::launch::async,
            [processId, trainer]() {
                return Loader::InjectTrainer(
                    processId,
                    trainer);
            });
    }

    void DrawStatus(
        ViewState state,
        const std::string& detail,
        float scale) {
        const ImVec4 success(
            0.30f, 0.88f, 0.59f, 1.0f);
        const ImVec4 warning(
            0.95f, 0.70f, 0.30f, 1.0f);
        const ImVec4 error(
            0.95f, 0.38f, 0.40f, 1.0f);
        const ImVec4 muted(
            0.48f, 0.51f, 0.54f, 1.0f);
        const ImVec4 tint = state == ViewState::Loaded
            || state == ViewState::Ready
                ? success
                : state == ViewState::Error
                    ? error
                    : state == ViewState::Loading
                        || state == ViewState::Analyzing
                        ? warning
                        : muted;

        const ImVec2 dotCenter(
            ImGui::GetCursorScreenPos().x + 5.0f * scale,
            ImGui::GetCursorScreenPos().y + 8.0f * scale);
        float radius = 3.0f * scale;
        if (state == ViewState::Loading
            || state == ViewState::Analyzing) {
            radius += (
                std::sin(
                    static_cast<float>(
                        ImGui::GetTime()) * 5.0f)
                + 1.0f) * scale;
        }
        ImGui::GetWindowDrawList()->AddCircleFilled(
            dotCenter,
            radius,
            Color(tint));
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() + 17.0f * scale);
        ImGui::PushFont(gSmallFont);
        const char* label =
            state == ViewState::Loaded
                ? "TRAINER LOADED"
                : state == ViewState::Ready
                    ? "READY"
                    : state == ViewState::Analyzing
                        ? "LOCAL ANALYSIS"
                    : state == ViewState::Loading
                        ? "LOADING"
                        : state == ViewState::Error
                            ? "ACTION NEEDED"
                            : "WAITING FOR GAME";
        ImGui::TextColored(tint, "%s", label);
        ImGui::PopFont();

        ImGui::PushFont(gBodyFont);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(0.61f, 0.64f, 0.67f, 1.0f));
        ImGui::PushTextWrapPos(
            ImGui::GetCursorPosX()
                + ImGui::GetContentRegionAvail().x);
        ImGui::TextUnformatted(detail.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    void DrawApp(
        HWND window,
        AppState& app,
        float scale) {
        RefreshState(app);

        ImGuiViewport* viewport =
            ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::Begin(
            "##loader_shell",
            nullptr,
            ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoScrollWithMouse
                | ImGuiWindowFlags_NoScrollbar);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 origin =
            ImGui::GetWindowPos();
        const ImVec2 size =
            ImGui::GetWindowSize();
        draw->AddRectFilled(
            origin,
            ImVec2(origin.x + size.x, origin.y + size.y),
            IM_COL32(9, 10, 11, 255),
            14.0f * scale);
        draw->AddRect(
            origin,
            ImVec2(origin.x + size.x, origin.y + size.y),
            IM_COL32(42, 46, 49, 255),
            14.0f * scale);
        draw->AddLine(
            ImVec2(
                origin.x + 1.0f,
                origin.y + 76.0f * scale),
            ImVec2(
                origin.x + size.x - 1.0f,
                origin.y + 76.0f * scale),
            IM_COL32(34, 37, 40, 255));

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool closeHovered =
            mouse.x >= origin.x + size.x - 54.0f * scale
            && mouse.x <= origin.x + size.x
            && mouse.y >= origin.y
            && mouse.y <= origin.y + 54.0f * scale;
        const ImVec2 closeCenter(
            origin.x + size.x - 27.0f * scale,
            origin.y + 27.0f * scale);
        const ImU32 closeColor = closeHovered
            ? IM_COL32(232, 235, 237, 255)
            : IM_COL32(102, 107, 112, 255);
        draw->AddLine(
            ImVec2(
                closeCenter.x - 4.0f * scale,
                closeCenter.y - 4.0f * scale),
            ImVec2(
                closeCenter.x + 4.0f * scale,
                closeCenter.y + 4.0f * scale),
            closeColor,
            1.5f * scale);
        draw->AddLine(
            ImVec2(
                closeCenter.x + 4.0f * scale,
                closeCenter.y - 4.0f * scale),
            ImVec2(
                closeCenter.x - 4.0f * scale,
                closeCenter.y + 4.0f * scale),
            closeColor,
            1.5f * scale);

        const float gutter = 30.0f * scale;
        ImGui::SetCursorPos(
            ImVec2(gutter, 20.0f * scale));
        ImGui::PushFont(gTitleFont);
        ImGui::TextUnformatted("Solarpunk Trainer");
        ImGui::PopFont();
        ImGui::SetCursorPos(
            ImVec2(gutter, 48.0f * scale));
        ImGui::PushFont(gSmallFont);
        ImGui::TextColored(
            ImVec4(0.40f, 0.43f, 0.46f, 1.0f),
            "LOCAL LOADER  /  X64");
        ImGui::PopFont();

        ImGui::SetCursorPos(
            ImVec2(gutter, 104.0f * scale));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(
            ImGui::GetCursorPosX()
                + size.x - gutter * 2.0f);
        DrawStatus(app.View, app.Detail, scale);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();

        const float buttonHeight = 44.0f * scale;
        const float buttonY =
            size.y - gutter - buttonHeight;
        ImGui::SetCursorPos(
            ImVec2(gutter, buttonY));
        const bool enabled =
            app.View == ViewState::Ready
            || app.View == ViewState::Error
                && app.ProcessId
                && app.TrainerAvailable
                && app.SchemaProbeAvailable;
        const char* buttonLabel =
            app.View == ViewState::Analyzing
                ? "Analyzing installed game..."
                : app.View == ViewState::Loading
                ? "Loading trainer..."
                : app.View == ViewState::Loaded
                    ? "Trainer loaded"
                    : app.View == ViewState::Error
                        ? "Retry local analysis"
                    : "Load trainer";
        if (PrimaryButton(
            "##load_trainer",
            buttonLabel,
            ImVec2(
                size.x - gutter * 2.0f,
                buttonHeight),
            enabled)) {
            if (app.View == ViewState::Ready)
                BeginInjection(app);
            else
                BeginCompatibility(app);
        }

        ImGui::End();
    }

    LRESULT WINAPI WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(
            window,
            message,
            wParam,
            lParam)) {
            return true;
        }

        switch (message) {
        case WM_NCCALCSIZE:
            if (wParam)
                return 0;
            break;
        case WM_NCHITTEST: {
            const LRESULT base =
                DefWindowProcW(
                    window,
                    message,
                    wParam,
                    lParam);
            if (base != HTCLIENT)
                return base;

            POINT point{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };
            ScreenToClient(window, &point);
            RECT client{};
            GetClientRect(window, &client);
            const UINT dpi = GetDpiForWindow(window);
            const int header = MulDiv(58, dpi, 96);
            const int closeWidth = MulDiv(54, dpi, 96);
            if (point.y >= 0 && point.y < header) {
                if (point.x
                    >= client.right - closeWidth) {
                    return HTCLOSE;
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_NCLBUTTONDOWN:
            if (wParam == HTCLOSE) {
                PostMessageW(window, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
        case WM_SIZE:
            if (gDevice
                && wParam != SIZE_MINIMIZED) {
                CleanupRenderTarget();
                gSwapChain->ResizeBuffers(
                    0,
                    LOWORD(lParam),
                    HIWORD(lParam),
                    DXGI_FORMAT_UNKNOWN,
                    0);
                CreateRenderTarget();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU)
                return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(
            window,
            message,
            wParam,
            lParam);
    }

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int) {
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW windowClass{
        sizeof(windowClass),
        CS_CLASSDC,
        WindowProcedure,
        0,
        0,
        instance,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        WindowClassName,
        nullptr
    };
    RegisterClassExW(&windowClass);

    const UINT systemDpi = GetDpiForSystem();
    const float scale =
        static_cast<float>(systemDpi) / 96.0f;
    const int width = static_cast<int>(
        LogicalWindowWidth * scale);
    const int height = static_cast<int>(
        LogicalWindowHeight * scale);
    const int screenWidth =
        GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight =
        GetSystemMetrics(SM_CYSCREEN);

    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW,
        windowClass.lpszClassName,
        L"Solarpunk Trainer",
        WS_OVERLAPPEDWINDOW
            & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX),
        (screenWidth - width) / 2,
        (screenHeight - height) / 2,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!window) {
        UnregisterClassW(
            windowClass.lpszClassName,
            instance);
        return 1;
    }

    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(
        window,
        20,
        &darkMode,
        sizeof(darkMode));
    enum class CornerPreference : DWORD {
        Round = 2
    };
    CornerPreference corner =
        CornerPreference::Round;
    DwmSetWindowAttribute(
        window,
        33,
        &corner,
        sizeof(corner));

    if (!CreateDeviceD3D(window)) {
        CleanupDeviceD3D();
        DestroyWindow(window);
        UnregisterClassW(
            windowClass.lpszClassName,
            instance);
        return 1;
    }

    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard;
    ApplyTheme(scale);
    if (!LoadFonts(scale)) {
        gBodyFont = io.Fonts->AddFontDefault();
        gTitleFont = gBodyFont;
        gSmallFont = gBodyFont;
    }

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(gDevice, gDeviceContext);

    AppState app{};
    bool running = true;
    while (running) {
        MSG message{};
        while (PeekMessageW(
            &message,
            nullptr,
            0,
            0,
            PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT)
                running = false;
        }
        if (!running)
            break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawApp(window, app, scale);
        ImGui::Render();

        constexpr float clearColor[4]{
            0.035f, 0.039f, 0.043f, 1.0f
        };
        gDeviceContext->OMSetRenderTargets(
            1,
            &gRenderTarget,
            nullptr);
        gDeviceContext->ClearRenderTargetView(
            gRenderTarget,
            clearColor);
        ImGui_ImplDX11_RenderDrawData(
            ImGui::GetDrawData());
        gSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(window);
    UnregisterClassW(
        windowClass.lpszClassName,
        instance);
    return 0;
}
