#include "Hashing.h"
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace sw
{

    std::string hashString(const std::string &input)
    {
        // FNV-1a 64-bit — fast, non-cryptographic
        constexpr uint64_t fnvOffset = 14695981039346656037ULL;
        constexpr uint64_t fnvPrime = 1099511628211ULL;

        uint64_t hash = fnvOffset;
        for (char c : input)
        {
            hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
            hash *= fnvPrime;
        }

        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(16) << hash;
        return oss.str();
    }

} // namespace sw
