#pragma once

#include <Windows.h>
#include <atomic>
#include <d3d11.h>
#include <dxgi.h>

typedef HRESULT( __stdcall* Present_t )( IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags );

class Render {
private:
    static uintptr_t game_present;
    static Present_t oPresent;
    static HMODULE   moduleHandle;
    static std::atomic_bool unloadRequested;

    static HWND      window;
    static WNDPROC   oWndProc;

    static bool ResolvePresent();
    static void CleanupRenderTarget();
    static void UpdateMenuCursorState(bool menuOpen);

public:
    static void SetModuleHandle(HMODULE module);
    static bool InstallPresentHook();
    static void Unload();
    static HWND GetGameWindow();
    static bool IsGameFocused();
    static bool IsUnloadRequested();

    static HRESULT __stdcall hkPresent( IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags );
    static LRESULT CALLBACK  hkWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );
};
