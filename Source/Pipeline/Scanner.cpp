#include "Scanner.h"
#include <JuceHeader.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

namespace sw
{

    namespace
    {
        std::optional<uint32_t> readU32LE(const uint8_t *data, size_t size, size_t offset)
        {
            if (offset + 4 > size)
                return std::nullopt;

            return static_cast<uint32_t>(data[offset]) |
                   (static_cast<uint32_t>(data[offset + 1]) << 8) |
                   (static_cast<uint32_t>(data[offset + 2]) << 16) |
                   (static_cast<uint32_t>(data[offset + 3]) << 24);
        }

        std::optional<uint16_t> readU16LE(const uint8_t *data, size_t size, size_t offset)
        {
            if (offset + 2 > size)
                return std::nullopt;

            return static_cast<uint16_t>(
                static_cast<uint16_t>(data[offset]) |
                static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8));
        }

        std::optional<float> readF32LE(const uint8_t *data, size_t size, size_t offset)
        {
            const auto raw = readU32LE(data, size, offset);
            if (!raw.has_value())
                return std::nullopt;

            float value = 0.0f;
            static_assert(sizeof(value) == sizeof(uint32_t));
            std::memcpy(&value, &raw.value(), sizeof(value));
            return value;
        }

        juce::String midiNoteName(int midiNote)
        {
            static constexpr const char *kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            if (midiNote < 0 || midiNote > 127)
                return {};

            const int octave = (midiNote / 12) - 1;
            return juce::String(kNames[midiNote % 12]) + juce::String(octave);
        }

        void parseWavAcidAndLoopMetadata(const std::filesystem::path &path, FileRecord &rec)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return;

            std::array<char, 12> riffHeader{};
            in.read(riffHeader.data(), static_cast<std::streamsize>(riffHeader.size()));
            if (in.gcount() != static_cast<std::streamsize>(riffHeader.size()))
                return;

            if (std::memcmp(riffHeader.data(), "RIFF", 4) != 0 || std::memcmp(riffHeader.data() + 8, "WAVE", 4) != 0)
                return;

            while (in)
            {
                std::array<char, 8> chunkHeader{};
                in.read(chunkHeader.data(), static_cast<std::streamsize>(chunkHeader.size()));
                if (in.gcount() != static_cast<std::streamsize>(chunkHeader.size()))
                    break;

                const char *chunkId = chunkHeader.data();
                const uint32_t chunkSize = static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[4])) |
                                           (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[5])) << 8) |
                                           (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[6])) << 16) |
                                           (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[7])) << 24);

                std::vector<uint8_t> chunkData(chunkSize);
                if (chunkSize > 0)
                {
                    in.read(reinterpret_cast<char *>(chunkData.data()), static_cast<std::streamsize>(chunkSize));
                    if (in.gcount() != static_cast<std::streamsize>(chunkSize))
                        break;
                }

                if ((chunkSize & 1u) != 0u)
                    in.seekg(1, std::ios::cur);

                if (std::memcmp(chunkId, "acid", 4) == 0)
                {
                    rec.loopType = std::string("acidized");

                    if (const auto rootNote = readU16LE(chunkData.data(), chunkData.size(), 4))
                    {
                        rec.acidRootNote = static_cast<int>(*rootNote);
                        if (!rec.key.has_value())
                        {
                            const auto noteName = midiNoteName(static_cast<int>(*rootNote));
                            if (noteName.isNotEmpty())
                                rec.key = noteName.toStdString();
                        }
                    }

                    if (const auto beats = readU32LE(chunkData.data(), chunkData.size(), 8))
                        rec.acidBeats = static_cast<int>(*beats);

                    if (const auto tempo = readF32LE(chunkData.data(), chunkData.size(), 12))
                    {
                        const double bpm = static_cast<double>(*tempo);
                        if (bpm > 0.0 && bpm < 400.0)
                            rec.bpm = bpm;
                    }
                }
                else if (std::memcmp(chunkId, "smpl", 4) == 0)
                {
                    const auto numLoops = readU32LE(chunkData.data(), chunkData.size(), 28);
                    if (!numLoops.has_value() || *numLoops == 0)
                        continue;

                    const size_t firstLoopOffset = 36;
                    if (firstLoopOffset + 24 > chunkData.size())
                        continue;

                    const auto loopStart = readU32LE(chunkData.data(), chunkData.size(), firstLoopOffset + 8);
                    const auto loopEnd = readU32LE(chunkData.data(), chunkData.size(), firstLoopOffset + 12);
                    if (loopStart.has_value() && loopEnd.has_value() && *loopEnd >= *loopStart)
                    {
                        rec.loopStartSample = static_cast<int64_t>(*loopStart);
                        rec.loopEndSample = static_cast<int64_t>(*loopEnd);
                    }
                }
            }

            if (rec.loopType.has_value() && *rec.loopType == "acidized" &&
                !rec.loopStartSample.has_value() && rec.acidBeats.has_value() &&
                rec.bpm.has_value() && rec.sampleRate.has_value())
            {
                const double estimatedSeconds = (60.0 * static_cast<double>(*rec.acidBeats)) / *rec.bpm;
                const auto estimatedSamples = static_cast<int64_t>(std::round(estimatedSeconds * static_cast<double>(*rec.sampleRate)));
                if (estimatedSamples > 1)
                {
                    rec.loopStartSample = 0;
                    rec.loopEndSample = estimatedSamples - 1;
                }
            }
        }
    }

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

                juce::AudioFormatManager formatManager;
                formatManager.registerBasicFormats();

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

                    if (playable)
                    {
                        if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(juce::File(path.string()))))
                        {
                            rec.totalSamples = static_cast<int64_t>(reader->lengthInSamples);
                            rec.sampleRate = static_cast<int>(reader->sampleRate);
                            rec.channels = static_cast<int>(reader->numChannels);
                            rec.bitDepth = reader->bitsPerSample;
                            rec.codec = reader->getFormatName().toStdString();

                            if (reader->sampleRate > 0.0)
                                rec.durationSec = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;

                            if (rec.durationSec.has_value() && *rec.durationSec > 0.0 && rec.sizeBytes > 0)
                            {
                                const auto kbps = static_cast<int>((static_cast<double>(rec.sizeBytes) * 8.0) / (*rec.durationSec * 1000.0));
                                rec.bitrateKbps = kbps;
                            }

                            if (ext == "wav")
                                parseWavAcidAndLoopMetadata(path, rec);
                        }
                    }

                    catalogDb.upsertFile(rec);

                    if (onProgress)
                        onProgress(relPath);
                }

                notifyCompleted();
            },
            JobPriority::Low});
    }

} // namespace sw
