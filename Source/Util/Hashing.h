#pragma once

#include <string>

namespace sw
{

    /// Compute a simple hex-string hash of the given input.
    /// Used for cache keys (not cryptographic).
    std::string hashString(const std::string &input);

} // namespace sw
