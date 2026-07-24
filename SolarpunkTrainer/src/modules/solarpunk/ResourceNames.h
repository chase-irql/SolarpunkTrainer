#pragma once

#include <cstddef>
#include <string_view>

namespace Solarpunk::ResourceNames {

    std::string_view Resolve(std::string_view rawClassName);
    bool HasFriendlyName(std::string_view rawClassName);
    size_t MappingCount();

} // namespace Solarpunk::ResourceNames
