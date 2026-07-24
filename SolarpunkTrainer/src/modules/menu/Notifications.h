#pragma once

#include <string>
#include <string_view>

namespace Notifications {

    enum class Kind {
        Info,
        Success,
        Warning,
        Error
    };

    void Push(
        Kind kind,
        std::string_view key,
        std::string title,
        std::string message = {},
        float durationSeconds = 4.25f);

    void Render();
    void Clear();

} // namespace Notifications
