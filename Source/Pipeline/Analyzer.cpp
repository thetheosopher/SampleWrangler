#include "Analyzer.h"
#include "Util/Paths.h"
#include <algorithm>
#include <JuceHeader.h>

namespace sw
{

    Analyzer::Analyzer(CatalogDb &db, JobQueue &queue)
        : catalogDb(db), jobQueue(queue)
    {
    }

    void Analyzer::analyzeFile(int64_t fileId)
    {
        jobQueue.enqueue(Job{
            [this, fileId](const std::atomic<uint64_t> &cancelGeneration, uint64_t jobGeneration)
            {
                if (cancelGeneration.load(std::memory_order_relaxed) != jobGeneration)
                    return;

                auto maybeFile = catalogDb.fileById(fileId);
                if (!maybeFile.has_value())
                    return;

                auto rec = std::move(*maybeFile);
                if (rec.indexOnly)
                    return; // can't analyze index-only formats

                const auto roots = catalogDb.allRoots();
                const auto rootIt = std::find_if(roots.begin(), roots.end(), [rootId = rec.rootId](const RootRecord &root)
                                                 { return root.id == rootId; });

                if (rootIt == roots.end())
                    return;

                const auto absolutePath = resolveAbsolutePath(rootIt->path, rec.relativePath);

                juce::AudioFormatManager formatManager;
                formatManager.registerBasicFormats();

                const juce::File sourceFile(absolutePath);
                auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(sourceFile));
                if (!reader)
                    return;

                if (reader->sampleRate > 0.0)
                    rec.durationSec = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
                else
                    rec.durationSec = std::nullopt;

                rec.sampleRate = static_cast<int>(reader->sampleRate);
                rec.channels = static_cast<int>(reader->numChannels);
                rec.bitDepth = reader->bitsPerSample;

                catalogDb.upsertFile(rec);
            },
            JobPriority::Normal});
    }

    void Analyzer::analyzeRoot(int64_t rootId)
    {
        const auto files = catalogDb.listRecentFiles(1000000);
        for (const auto &file : files)
        {
            if (file.rootId != rootId)
                continue;

            if (file.indexOnly)
                continue;

            if (file.durationSec.has_value())
                continue;

            analyzeFile(file.id);
        }
    }

} // namespace sw
