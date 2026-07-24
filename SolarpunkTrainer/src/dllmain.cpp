#include <Windows.h>
#include <iostream>
#include <minhook/include/MinHook.h>
#include "modules\render\Render.h"
#include "modules\solarpunk\GameSchema.h"
#include "modules\solarpunk\PlayerExploits.h"

DWORD WINAPI MainThread( LPVOID lpReserved ) {
    AllocConsole();
    FILE* f;
    freopen_s( &f, "CONOUT$", "w", stdout );
    std::cout << "[+] Mod Injected Successfully!" << std::endl;

    Render::SetModuleHandle(static_cast<HMODULE>(lpReserved));

    if (!Solarpunk::GameSchema::Initialize()) {
        std::cout
            << "[-] Local compatibility schema rejected: "
            << Solarpunk::GameSchema::Status()
            << std::endl;
        if (f) fclose(f);
        FreeConsole();
        FreeLibraryAndExitThread(
            static_cast<HMODULE>(lpReserved),
            0);
        return 0;
    }
    std::cout
        << "[+] "
        << Solarpunk::GameSchema::Status()
        << std::endl;

    if (MH_Initialize() != MH_OK) {
        std::cout << "[-] MinHook initialization failed." << std::endl;
        FreeLibraryAndExitThread( (HMODULE)lpReserved, 0 );
        return 0;
    }

    // Present hook before UnityBridge so DX11 is ready
    if (Render::InstallPresentHook()) {
        std::cout << "[+] DirectX 11 Hook Installed." << std::endl;
    }
    else {
        std::cout << "[-] Failed to hook Present." << std::endl;
        MH_Uninitialize();
        if (f) fclose( f );
        FreeConsole();
        FreeLibraryAndExitThread( static_cast<HMODULE>(lpReserved), 0 );
        return 0;
    }

    if (Solarpunk::PlayerExploits::InstallRuntimeHook()) {
        std::cout
            << "[+] Crafting and research transaction hook installed."
            << std::endl;
    }
    else {
        std::cout
            << "[-] Crafting and research transaction hook unavailable."
            << std::endl;
    }

    while (!Render::IsUnloadRequested()) {
        Sleep( 100 );
    }

    std::cout << "[!] Unloading Mod..." << std::endl;

    Render::Unload();
    MH_DisableHook( MH_ALL_HOOKS );
    MH_Uninitialize();

    if (f) fclose( f );
    FreeConsole();
    FreeLibraryAndExitThread( (HMODULE)lpReserved, 0 );
    return 0;
}

BOOL WINAPI DllMain( HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved ) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls( hinstDLL );
        if (HANDLE hThread = CreateThread( nullptr, 0, MainThread, hinstDLL, 0, nullptr ))
            CloseHandle( hThread );
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
