#include "Scanner.h"
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace sw
{

    Scanner::Scanner(CatalogDb &db, JobQueue &queue)
        : catalogDb(db), jobQueue(queue)
    {
    }

    bool Scanner::isPlayableExtension(const std::string &ext)
    {
        return ext == "wav" || ext == "aif" || ext == "aiff" || ext == "flac" || ext == "mp3";
    }

    bool Scanner::isIndexOnlyExtension(const std::string &ext)
    {
        return ext == "rex" || ext == "rex2" || ext == "rx2" || ext == "nki" || ext == "sfz";
    }

    void Scanner::scanRoot(int64_t rootId,
                           const std::string &rootPath,
                           ProgressCallback onProgress,
                           CompletionCallback onCompleted)
    {
        jobQueue.enqueue(Job{
            [this, rootId, rootPath, onProgress, onCompleted](const std::atomic<uint64_t> &cancelGeneration, uint64_t jobGeneration)
            {
                const auto notifyCompleted = [&onCompleted]()
                {
                    if (onCompleted)
                        onCompleted();
                };

                const auto isCancelled = [&cancelGeneration, jobGeneration]()
                {
                    return cancelGeneration.load(std::memory_order_relaxed) != jobGeneration;
                };

                std::error_code ec;
                for (auto it = fs::recursive_directory_iterator(rootPath,
                                                                fs::directory_options::skip_permission_denied, ec);
                     it != fs::recursive_directory_iterator(); ++it)
                {
                    if (isCancelled())
                    {
                        notifyCompleted();
                        return;
                    }

                    if (!it->is_regular_file(ec))
                        continue;

                    const auto &path = it->path();
                    std::string ext = path.extension().string();
                    if (!ext.empty() && ext[0] == '.')
                        ext = ext.substr(1);
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c)
                                   { return static_cast<char>(std::tolower(c)); });

                    const bool playable = isPlayableExtension(ext);
                    const bool indexOnly = isIndexOnlyExtension(ext);
                    if (!playable && !indexOnly)
                        continue;

                    // Build relative path from root
                    auto relPath = fs::relative(path, rootPath, ec).generic_string();
                    if (ec)
                        continue;

                    FileRecord rec;
                    rec.rootId = rootId;
                    rec.relativePath = relPath;
                    rec.filename = path.filename().string();
                    rec.extension = ext;
                    rec.indexOnly = indexOnly;

                    // File metadata
                    rec.sizeBytes = static_cast<int64_t>(fs::file_size(path, ec));
                    auto ftime = fs::last_write_time(path, ec);
                    rec.modifiedTime = static_cast<int64_t>(
                        std::chrono::duration_cast<std::chrono::seconds>(
                            ftime.time_since_epoch())
                            .count());

                    catalogDb.upsertFile(rec);

                    if (onProgress)
                        onProgress(relPath);
                }

                notifyCompleted();
            },
            JobPriority::Low});
    }

} // namespace sw
