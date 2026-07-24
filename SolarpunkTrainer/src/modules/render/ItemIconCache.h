#pragma once

#include <cstdint>

struct ID3D11Device;

namespace ItemIconCache {

    // Rebuilds the item-class lookup when the active game instance or the
    // backing Unreal TMap allocation changes. Call from the Present thread.
    void Update(uintptr_t gameInstance, ID3D11Device* device);

    // Returns an ImGui-compatible DX11 texture identifier (an
    // ID3D11ShaderResourceView pointer encoded as an integer), or zero when
    // the class/icon is unavailable.
    uintptr_t GetTextureId(uintptr_t itemClass);

    // Releases every retained shader-resource view. Must run before the
    // renderer device is torn down.
    void Reset();

} // namespace ItemIconCache
