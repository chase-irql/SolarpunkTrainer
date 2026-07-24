#include "Render.h"
#include "../menu/Menu.h"
#include "../menu/Notifications.h"
#include "../solarpunk/FlyHack.h"
#include "../solarpunk/InventoryEditor.h"
#include "../solarpunk/PlayerExploits.h"
#include "../solarpunk/WaypointSystem.h"
#include "ItemIconCache.h"
#include "WorldRenderer.h"
#include <iostream>
#include <memory/Memory.h>
#include <MinHook/include/MinHook.h>
#include <ImGui/imgui.h>
#include <ImGui/imgui_impl_dx11.h>
#include <ImGui/imgui_impl_win32.h>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

uintptr_t            Render::game_present = 0;
Present_t            Render::oPresent = nullptr;
HMODULE              Render::moduleHandle = nullptr;
std::atomic_bool      Render::unloadRequested = false;
HWND                 Render::window = nullptr;
WNDPROC              Render::oWndProc = nullptr;

static bool                  imguiInitialized = false;
static ID3D11Device* pDevice = nullptr;
static ID3D11DeviceContext* pContext = nullptr;
static ID3D11RenderTargetView* mainRenderTargetView = nullptr;

namespace {

    // Solar Punk's UE raw-input thread uses these private messages:
    // WM_APP + 1 registers its mouse and WM_APP + 2 unregisters it.
    constexpr UINT UeRegisterRawInputMessage = WM_APP + 1;
    constexpr UINT UeUnregisterRawInputMessage = WM_APP + 2;

    bool gMenuOwnsCursor = false;
    bool gSavedClipRectValid = false;
    RECT gSavedClipRect{};

    HWND gRawInputWindow = nullptr;
    bool gRawInputSuspended = false;

    UINT GetReleaseMenuCursorMessage() {
        static const UINT message = RegisterWindowMessageW(
            L"SolarpunkTrainer.ReleaseMenuCursor.v1");
        return message;
    }

    HWND FindUeRawInputWindow(HWND mainWindow) {
        UINT deviceCount = 0;
        if (GetRegisteredRawInputDevices(
            nullptr,
            &deviceCount,
            sizeof(RAWINPUTDEVICE)) == static_cast<UINT>(-1)
            || deviceCount == 0) {
            return nullptr;
        }

        std::vector<RAWINPUTDEVICE> devices(deviceCount);
        UINT returnedCount = deviceCount;
        if (GetRegisteredRawInputDevices(
            devices.data(),
            &returnedCount,
            sizeof(RAWINPUTDEVICE)) == static_cast<UINT>(-1)) {
            return nullptr;
        }

        for (UINT index = 0; index < returnedCount; ++index) {
            const RAWINPUTDEVICE& device = devices[index];
            if (device.usUsagePage == 0x01
                && device.usUsage == 0x02
                && device.hwndTarget
                && device.hwndTarget != mainWindow
                && IsWindow(device.hwndTarget)) {
                return device.hwndTarget;
            }
        }

        return nullptr;
    }

    void SuspendUeRawInput(HWND mainWindow) {
        if (gRawInputSuspended)
            return;

        gRawInputWindow = FindUeRawInputWindow(mainWindow);
        if (gRawInputWindow
            && PostMessageW(
                gRawInputWindow,
                UeUnregisterRawInputMessage,
                0,
                0)) {
            gRawInputSuspended = true;
        }
    }

    void ResumeUeRawInput() {
        if (gRawInputSuspended
            && gRawInputWindow
            && IsWindow(gRawInputWindow)) {
            PostMessageW(
                gRawInputWindow,
                UeRegisterRawInputMessage,
                0,
                0);
        }

        gRawInputSuspended = false;
        gRawInputWindow = nullptr;
    }

    bool IsKeyboardMessage(UINT msg) {
        switch (msg) {
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_UNICHAR:
            return true;
        default:
            return false;
        }
    }

} // namespace

// ─── Internal helpers ────────────────────────────────────────────────────────

void Render::CleanupRenderTarget() {
    if (mainRenderTargetView) {
        mainRenderTargetView->Release();
        mainRenderTargetView = nullptr;
    }
}

// ─── Unload ──────────────────────────────────────────────────────────────────

void Render::Unload() {
    // 1. Disable + remove the Present hook first so hkPresent stops being called
    //    before we tear down the resources it uses.
    if (game_present) {
        MH_DisableHook( reinterpret_cast<LPVOID>( game_present ) );
        MH_RemoveHook( reinterpret_cast<LPVOID>( game_present ) );
        game_present = 0;
    }

    // 2. Restore the pawn state only after no new Present callback can update it.
    Solarpunk::FlyHack::Shutdown();
    Solarpunk::PlayerExploits::Shutdown(window);
    Solarpunk::InventoryEditor::Reset();
    Solarpunk::WaypointSystem::ResetRuntime();
    ItemIconCache::Reset();

    // 3. Return cursor confinement and UE raw input to their pre-menu state.
    UpdateMenuCursorState(false);

    // 4. Restore original WndProc so the game stops routing through hkWndProc.
    if (window && oWndProc) {
        SetWindowLongPtr( window, GWLP_WNDPROC, (LONG_PTR)oWndProc );
        oWndProc = nullptr;
    }

    // 5. Shut down ImGui backends (order matters: DX11 first, then Win32).
    if (imguiInitialized) {
        Notifications::Clear();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized = false;
    }

    // 6. Release D3D11 resources.
    CleanupRenderTarget();

    if (pContext) { pContext->Release(); pContext = nullptr; }
    if (pDevice) { pDevice->Release();  pDevice = nullptr; }
}

// ─── WndProc hook ────────────────────────────────────────────────────────────

LRESULT CALLBACK Render::hkWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam ) {
    const UINT inventoryEditMessage =
        Solarpunk::InventoryEditor::GetApplyMessage();
    if (inventoryEditMessage != 0 && msg == inventoryEditMessage) {
        // UE's main HWND dispatches on the game thread. Inventory mutations
        // are marshalled here so ProcessEvent never runs from Present.
        Solarpunk::InventoryEditor::ProcessPendingEdit();
        return 0;
    }

    const UINT waypointTeleportMessage =
        Solarpunk::WaypointSystem::GetTeleportMessage();
    if (waypointTeleportMessage != 0
        && msg == waypointTeleportMessage) {
        Solarpunk::WaypointSystem::ProcessPendingTeleport();
        return 0;
    }

    const UINT exploitRuntimeMessage =
        Solarpunk::PlayerExploits::GetRuntimeUpdateMessage();
    if (exploitRuntimeMessage != 0
        && msg == exploitRuntimeMessage) {
        // Camera-component, world-time, and possessed-airship changes use
        // the same main-HWND game-thread dispatch as inventory mutations.
        Solarpunk::PlayerExploits::ProcessPendingRuntimeUpdate();
        return 0;
    }

    const UINT releaseCursorMessage = GetReleaseMenuCursorMessage();
    if (releaseCursorMessage != 0 && msg == releaseCursorMessage) {
        if (wParam != FALSE) {
            ClipCursor(nullptr);
            if (GetCapture())
                ReleaseCapture();
        }
        return 0;
    }

    // Always feed input to ImGui.
    const LRESULT imguiResult =
        ImGui_ImplWin32_WndProcHandler( hWnd, msg, wParam, lParam );

    if (msg == WM_KILLFOCUS || (msg == WM_ACTIVATEAPP && wParam == FALSE))
        Solarpunk::FlyHack::SuspendInput();

    if (Menu::IsOpen()) {
        // ImGui owns the pointer while the menu is open. UE's separate
        // raw-input HWND is suspended during the same transition.
        ClipCursor(nullptr);

        // The Win32 backend hides the OS cursor while MouseDrawCursor is set.
        // Never forward WM_SETCURSOR to UE during menu ownership, or it may
        // restore its hardware cursor on top of ImGui's software cursor.
        if (msg == WM_SETCURSOR)
            return imguiResult ? imguiResult : TRUE;

        if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)
            || msg == WM_INPUT) {
            return 1;
        }
        if (IsKeyboardMessage(msg))
            return 1;
    }

    return CallWindowProc( oWndProc, hWnd, msg, wParam, lParam );
}

// ─── Pattern scan ────────────────────────────────────────────────────────────

bool Render::ResolvePresent() {
    game_present = Memory::PatternScanModule(
        L"dxgi.dll",
        "? ? ? ? ? 48 89 74 24 ? 55 57 41 56 48 8D 6C 24 ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 45 33 F6"
    );
    return game_present != 0;
}

// ─── Hook install ────────────────────────────────────────────────────────────

void Render::SetModuleHandle(HMODULE module) {
    moduleHandle = module;
}

void Render::UpdateMenuCursorState(bool menuOpen) {
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = menuOpen;

        // Outside the menu, cursor shape/visibility belongs entirely to UE.
        // While open, allow the Win32 backend to hide the OS cursor and let
        // ImGui draw one stable software cursor in the overlay.
        if (menuOpen)
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        else
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }

    if (menuOpen && !gMenuOwnsCursor) {
        gSavedClipRectValid = GetClipCursor(&gSavedClipRect) != FALSE;
        gMenuOwnsCursor = true;

        SuspendUeRawInput(window);
        SetCursor(nullptr);
        if (window) {
            const UINT releaseCursorMessage =
                GetReleaseMenuCursorMessage();
            if (releaseCursorMessage != 0) {
                PostMessageW(
                    window,
                    releaseCursorMessage,
                    TRUE,
                    0);
            }
        }
    }
    else if (!menuOpen && gMenuOwnsCursor) {
        ResumeUeRawInput();

        if (gSavedClipRectValid)
            ClipCursor(&gSavedClipRect);
        else
            ClipCursor(nullptr);

        gSavedClipRect = {};
        gSavedClipRectValid = false;
        gMenuOwnsCursor = false;
        return;
    }

    if (!menuOpen)
        return;

    // UE may try to reapply viewport confinement each tick. Releasing it here
    // keeps the cursor free before ImGui samples the next frame.
    ClipCursor(nullptr);
}

bool Render::IsGameFocused() {
    return window && GetForegroundWindow() == window;
}

HWND Render::GetGameWindow() {
    return window;
}

bool Render::IsUnloadRequested() {
    return unloadRequested.load(std::memory_order_acquire);
}

bool Render::InstallPresentHook() {
    unloadRequested.store(false, std::memory_order_release);
    if (!ResolvePresent())
        return false;

    if (MH_CreateHook(
        reinterpret_cast<LPVOID>( game_present ),
        &Render::hkPresent,
        reinterpret_cast<LPVOID*>( &Render::oPresent ) ) != MH_OK)
        return false;

    if (MH_EnableHook( reinterpret_cast<LPVOID>( game_present ) ) != MH_OK)
        return false;

    return true;
}

// ─── Present hook ────────────────────────────────────────────────────────────

HRESULT __stdcall Render::hkPresent( IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags ) {

    // ── Init (first frame only) ───────────────────────────────────────────
    if (!imguiInitialized) {
        if (FAILED( pSwapChain->GetDevice( __uuidof( ID3D11Device ), (void**)&pDevice ) ))
            return oPresent( pSwapChain, SyncInterval, Flags );

        pDevice->GetImmediateContext( &pContext );

        DXGI_SWAP_CHAIN_DESC sd{};
        pSwapChain->GetDesc( &sd );
        window = sd.OutputWindow;

        ID3D11Texture2D* pBackBuffer = nullptr;
        pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (LPVOID*)&pBackBuffer );
        pDevice->CreateRenderTargetView( pBackBuffer, nullptr, &mainRenderTargetView );
        pBackBuffer->Release();

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.MouseDrawCursor = false;

        Menu::Initialize(moduleHandle);
        ImGui_ImplWin32_Init( window );
        ImGui_ImplDX11_Init( pDevice, pContext );

        oWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr( window, GWLP_WNDPROC, (LONG_PTR)hkWndProc )
            );

        imguiInitialized = true;
    }

    // ── Per-frame ─────────────────────────────────────────────────────────
    UpdateMenuCursorState(Menu::IsOpen());

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const ImGuiIO& frameIO = ImGui::GetIO();
    WorldRenderer::BeginFrame(
        frameIO.DisplaySize.x,
        frameIO.DisplaySize.y);
    const bool gameFocused = IsGameFocused();

    static bool insertWasDown = false;
    static bool hudWasDown = false;
    static bool unloadWasDown = false;
    const bool insertDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    const bool hudDown = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
    const bool unloadDown = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
    const bool insertPressed = gameFocused && insertDown && !insertWasDown;
    const bool hudPressed = gameFocused && hudDown && !hudWasDown;
    const bool unloadPressed = gameFocused && unloadDown && !unloadWasDown;
    insertWasDown = insertDown;
    hudWasDown = hudDown;
    unloadWasDown = unloadDown;

    // Toggle menu with INSERT
    if (insertPressed) {
        Menu::Toggle();
        UpdateMenuCursorState(Menu::IsOpen());
    }

    // Toggle the lightweight coordinate HUD independently of the full menu.
    if (hudPressed)
        Menu::ToggleCoordinateOverlay();

    if (unloadPressed) {
        Solarpunk::FlyHack::Shutdown();
        if (Menu::IsOpen()) {
            Menu::Toggle();
            UpdateMenuCursorState(false);
        }
        unloadRequested.store(true, std::memory_order_release);
    }

    const auto& frameState = WorldRenderer::GetFrameState();
    if (Menu::IsOpen()) {
        ItemIconCache::Update(
            frameState.Player.GameInstance,
            pDevice);
    }
    Solarpunk::PlayerExploits::Update(frameState.Player, window);
    Solarpunk::FlyHack::Update(
        frameState.Player,
        frameState.Camera,
        frameIO.DeltaTime,
        gameFocused && !Menu::IsOpen());

    Menu::Render();
    UpdateMenuCursorState(Menu::IsOpen());

    ImGui::Render();
    pContext->OMSetRenderTargets( 1, &mainRenderTargetView, nullptr );
    ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );

    return oPresent( pSwapChain, SyncInterval, Flags );
}
