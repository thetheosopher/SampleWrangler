#include "Paths.h"
#include <filesystem>

#ifdef _WIN32
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace sw
{

    std::string resolveAbsolutePath(const std::string &rootPath, const std::string &relativePath)
    {
        return (fs::path(rootPath) / relativePath).string();
    }

    std::string appDataDirectory()
    {
#ifdef _WIN32
        wchar_t *rawPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &rawPath)))
        {
            fs::path p(rawPath);
            CoTaskMemFree(rawPath);
            return (p / "SampleWrangler").string();
        }
#endif
        // Fallback
        return (fs::current_path() / ".samplewrangler").string();
    }

    std::string defaultCacheDirectory()
    {
        return (fs::path(appDataDirectory()) / "cache").string();
    }

    std::string defaultDatabasePath()
    {
        return (fs::path(appDataDirectory()) / "catalog.db").string();
    }

} // namespace sw
