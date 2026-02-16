#pragma once

#include <string>
#include <optional>

namespace sw
{

    /// Resolve an absolute path from a source path and a relative path.
    std::string resolveAbsolutePath(const std::string &rootPath, const std::string &relativePath);

    /// Return the application data directory for SampleWrangler.
    /// e.g. C:\Users\<user>\AppData\Local\SampleWrangler
    std::string appDataDirectory();

    /// Return the default waveform cache directory inside appDataDirectory.
    std::string defaultCacheDirectory();

    /// Return the default database file path.
    std::string defaultDatabasePath();

} // namespace sw
