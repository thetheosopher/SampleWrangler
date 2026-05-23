#include "Scanner.h"
#include "AcpPresetReader.h"
#include "RexManager.h"
#include "WaveformPeak.h"
#include "Util/Hashing.h"
#include <juce_audio_formats/juce_audio_formats.h>
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
#include <unordered_map>
#include <memory>
#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace fs = std::filesystem;

namespace sw
{

    namespace
    {
        std::optional<std::string> buildContentHash(const std::filesystem::path &path, int64_t sizeBytes)
        {
            constexpr std::streamsize kSampleBytes = 64 * 1024;

            std::ifstream in(path, std::ios::binary);
            if (!in)
                return std::nullopt;

            std::string fingerprint;
            fingerprint.reserve(static_cast<size_t>(std::min<int64_t>(sizeBytes, static_cast<int64_t>(kSampleBytes * 2))) + 64);
            fingerprint.append(std::to_string(sizeBytes));
            fingerprint.push_back('|');

            std::vector<char> buffer(static_cast<size_t>(kSampleBytes));
            const auto appendChunk = [&](std::streamoff offset, std::streamsize requestedBytes)
            {
                in.clear();
                in.seekg(offset, std::ios::beg);
                if (!in.good())
                    return false;

                in.read(buffer.data(), requestedBytes);
                const auto bytesRead = in.gcount();
                if (bytesRead <= 0)
                    return false;

                fingerprint.append(buffer.data(), static_cast<size_t>(bytesRead));
                fingerprint.push_back('|');
                return true;
            };

            if (sizeBytes <= static_cast<int64_t>(kSampleBytes * 2))
            {
                if (!appendChunk(0, static_cast<std::streamsize>(sizeBytes)))
                    return std::nullopt;
            }
            else
            {
                if (!appendChunk(0, kSampleBytes))
                    return std::nullopt;

                if (!appendChunk(static_cast<std::streamoff>(sizeBytes - kSampleBytes), kSampleBytes))
                    return std::nullopt;
            }

            return hashString(fingerprint);
        }

        std::vector<float> buildOverviewPeaks(juce::AudioFormatReader &reader, int targetPeakCount)
        {
            const int64_t totalSamples = reader.lengthInSamples;
            if (totalSamples <= 0)
                return {};

            const int numChannels = std::max(1, static_cast<int>(reader.numChannels));
            const int peakCount = static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(targetPeakCount, totalSamples)));
            const int64_t samplesPerPeak = std::max<int64_t>(1, totalSamples / peakCount);

            std::vector<float> peaks(static_cast<size_t>(peakCount), 0.0f);
            juce::AudioBuffer<float> tempBuffer(numChannels, static_cast<int>(samplesPerPeak));

            for (int i = 0; i < peakCount; ++i)
            {
                const int64_t startSample = static_cast<int64_t>(i) * samplesPerPeak;
                const int64_t remaining = totalSamples - startSample;
                if (remaining <= 0)
                    break;

                const int blockSamples = static_cast<int>(std::min<int64_t>(samplesPerPeak, remaining));
                if (blockSamples <= 0)
                    break;

                tempBuffer.clear();
                if (!reader.read(&tempBuffer, 0, blockSamples, startSample, true, true))
                    break;

                float maxAbs = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float *channelData = tempBuffer.getReadPointer(ch);
                    for (int s = 0; s < blockSamples; ++s)
                        maxAbs = std::max(maxAbs, std::abs(channelData[s]));
                }

                peaks[static_cast<size_t>(i)] = maxAbs;
            }

            return peaks;
        }

        std::vector<float> buildOverviewPeaks(const juce::AudioBuffer<float> &buffer, int targetPeakCount)
        {
            const int totalSamples = buffer.getNumSamples();
            if (totalSamples <= 0)
                return {};

            const int numChannels = std::max(1, buffer.getNumChannels());
            const int peakCount = std::max(1, std::min(targetPeakCount, totalSamples));
            const int samplesPerPeak = std::max(1, totalSamples / peakCount);

            std::vector<float> peaks(static_cast<size_t>(peakCount), 0.0f);

            for (int i = 0; i < peakCount; ++i)
            {
                const int startSample = i * samplesPerPeak;
                const int remaining = totalSamples - startSample;
                if (remaining <= 0)
                    break;

                const int blockSamples = std::min(samplesPerPeak, remaining);
                float maxAbs = 0.0f;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float *channelData = buffer.getReadPointer(ch);
                    for (int s = 0; s < blockSamples; ++s)
                        maxAbs = std::max(maxAbs, std::abs(channelData[startSample + s]));
                }

                peaks[static_cast<size_t>(i)] = maxAbs;
            }

            return peaks;
        }

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

        std::optional<uint32_t> readU32BE(const uint8_t *data, size_t size, size_t offset)
        {
            if (offset + 4 > size)
                return std::nullopt;

            return (static_cast<uint32_t>(data[offset]) << 24) |
                   (static_cast<uint32_t>(data[offset + 1]) << 16) |
                   (static_cast<uint32_t>(data[offset + 2]) << 8) |
                   static_cast<uint32_t>(data[offset + 3]);
        }

        std::optional<uint16_t> readU16BE(const uint8_t *data, size_t size, size_t offset)
        {
            if (offset + 2 > size)
                return std::nullopt;

            return static_cast<uint16_t>(
                (static_cast<uint16_t>(data[offset]) << 8) |
                static_cast<uint16_t>(data[offset + 1]));
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

        std::string trimAsciiCopy(std::string value)
        {
            const auto isNotWhitespace = [](unsigned char c)
            {
                return !std::isspace(c);
            };

            const auto begin = std::find_if(value.begin(), value.end(), isNotWhitespace);
            if (begin == value.end())
                return {};

            const auto end = std::find_if(value.rbegin(), value.rend(), isNotWhitespace).base();
            return std::string(begin, end);
        }

        std::string lowerAsciiCopy(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        std::optional<int> parseIntegerValue(const std::string &value, int minValue, int maxValue)
        {
            const auto trimmed = trimAsciiCopy(value);
            if (trimmed.empty())
                return std::nullopt;

            char *parseEnd = nullptr;
            const long numericValue = std::strtol(trimmed.c_str(), &parseEnd, 10);
            if (parseEnd == nullptr || *parseEnd != '\0')
                return std::nullopt;

            if (numericValue < minValue || numericValue > maxValue)
                return std::nullopt;

            return static_cast<int>(numericValue);
        }

        void updateOptionalMin(std::optional<int> &target, int value)
        {
            if (!target.has_value() || value < *target)
                target = value;
        }

        void updateOptionalMax(std::optional<int> &target, int value)
        {
            if (!target.has_value() || value > *target)
                target = value;
        }

        std::optional<int> parseMidiNoteValue(const std::string &value);

        juce::String readAttributeIgnoreCase(const juce::XmlElement &element,
                                             std::initializer_list<const char *> candidateNames)
        {
            for (const auto *candidate : candidateNames)
            {
                if (element.hasAttribute(candidate))
                    return element.getStringAttribute(candidate);
            }

            for (int attributeIndex = 0; attributeIndex < element.getNumAttributes(); ++attributeIndex)
            {
                const auto &attributeName = element.getAttributeName(attributeIndex);
                for (const auto *candidate : candidateNames)
                {
                    if (attributeName.equalsIgnoreCase(candidate))
                        return element.getAttributeValue(attributeIndex);
                }
            }

            return {};
        }

        const juce::XmlElement *findFirstXmlElement(const juce::XmlElement &element,
                                                    std::initializer_list<const char *> tagNames)
        {
            for (const auto *tagName : tagNames)
            {
                if (element.getTagName().equalsIgnoreCase(tagName))
                    return &element;
            }

            forEachXmlChildElement(element, child)
            {
                if (const auto *match = findFirstXmlElement(*child, tagNames))
                    return match;
            }

            return nullptr;
        }

        void collectXmlElements(const juce::XmlElement &element,
                                std::initializer_list<const char *> tagNames,
                                std::vector<const juce::XmlElement *> &matches)
        {
            for (const auto *tagName : tagNames)
            {
                if (element.getTagName().equalsIgnoreCase(tagName))
                {
                    matches.push_back(&element);
                    break;
                }
            }

            forEachXmlChildElement(element, child)
                collectXmlElements(*child, tagNames, matches);
        }

        std::optional<int> readMidiAttributeIgnoreCase(const juce::XmlElement &element,
                                                       std::initializer_list<const char *> candidateNames)
        {
            return parseMidiNoteValue(readAttributeIgnoreCase(element, candidateNames).toStdString());
        }

        std::optional<int> readIntegerAttributeIgnoreCase(const juce::XmlElement &element,
                                                          std::initializer_list<const char *> candidateNames,
                                                          int minValue,
                                                          int maxValue)
        {
            return parseIntegerValue(readAttributeIgnoreCase(element, candidateNames).toStdString(), minValue, maxValue);
        }

        int findZipEntryIndexByName(const juce::ZipFile &zip, const juce::String &entryName)
        {
            if (entryName.isEmpty())
                return -1;

            for (int index = 0; index < zip.getNumEntries(); ++index)
            {
                const auto *entry = zip.getEntry(index);
                if (entry != nullptr && entry->filename.equalsIgnoreCase(entryName))
                    return index;
            }

            const auto baseName = juce::File::createFileWithoutCheckingPath(entryName).getFileName();
            for (int index = 0; index < zip.getNumEntries(); ++index)
            {
                const auto *entry = zip.getEntry(index);
                if (entry == nullptr)
                    continue;

                if (juce::File::createFileWithoutCheckingPath(entry->filename).getFileName().equalsIgnoreCase(baseName))
                    return index;
            }

            return -1;
        }

        std::optional<std::filesystem::path> resolveIndexedSamplePath(const std::filesystem::path &presetPath,
                                                                      const std::string &rawSamplePath)
        {
            if (rawSamplePath.empty())
                return std::nullopt;

            const auto normalized = juce::String(rawSamplePath).replaceCharacter('\\', '/').toStdString();
            const std::filesystem::path samplePath(normalized);
            std::error_code ec;

            if (samplePath.is_absolute() && std::filesystem::exists(samplePath, ec) && !ec)
                return samplePath;

            const auto presetFolder = presetPath.parent_path();
            const auto directCandidate = (presetFolder / samplePath).lexically_normal();
            if (std::filesystem::exists(directCandidate, ec) && !ec)
                return directCandidate;

            const auto samplesCandidate = (presetFolder / "Samples" / samplePath.filename()).lexically_normal();
            ec.clear();
            if (std::filesystem::exists(samplesCandidate, ec) && !ec)
                return samplesCandidate;

            auto parent = presetFolder.parent_path();
            for (int i = 0; i < 2 && !parent.empty(); ++i)
            {
                const auto parentCandidate = (parent / samplePath).lexically_normal();
                ec.clear();
                if (std::filesystem::exists(parentCandidate, ec) && !ec)
                    return parentCandidate;

                const auto parentSamplesCandidate = (parent / "Samples" / samplePath.filename()).lexically_normal();
                ec.clear();
                if (std::filesystem::exists(parentSamplesCandidate, ec) && !ec)
                    return parentSamplesCandidate;

                parent = parent.parent_path();
            }

            return std::nullopt;
        }

        std::optional<int> parseMidiNoteValue(const std::string &value)
        {
            const auto trimmed = trimAsciiCopy(value);
            if (trimmed.empty())
                return std::nullopt;

            char *parseEnd = nullptr;
            const long numericValue = std::strtol(trimmed.c_str(), &parseEnd, 10);
            if (parseEnd != nullptr && *parseEnd == '\0')
            {
                if (numericValue >= 0 && numericValue <= 127)
                    return static_cast<int>(numericValue);

                return std::nullopt;
            }

            const auto lowered = lowerAsciiCopy(trimmed);
            if (lowered.empty())
                return std::nullopt;

            int semitone = 0;
            switch (lowered[0])
            {
            case 'c':
                semitone = 0;
                break;
            case 'd':
                semitone = 2;
                break;
            case 'e':
                semitone = 4;
                break;
            case 'f':
                semitone = 5;
                break;
            case 'g':
                semitone = 7;
                break;
            case 'a':
                semitone = 9;
                break;
            case 'b':
                semitone = 11;
                break;
            default:
                return std::nullopt;
            }

            size_t octaveIndex = 1;
            if (lowered.size() > 1)
            {
                if (lowered[1] == '#')
                {
                    ++semitone;
                    octaveIndex = 2;
                }
                else if (lowered[1] == 'b')
                {
                    --semitone;
                    octaveIndex = 2;
                }
            }

            if (octaveIndex >= lowered.size())
                return std::nullopt;

            const auto octaveText = lowered.substr(octaveIndex);
            const long octave = std::strtol(octaveText.c_str(), &parseEnd, 10);
            if (parseEnd == nullptr || *parseEnd != '\0')
                return std::nullopt;

            while (semitone < 0)
                semitone += 12;

            const int midiNote = static_cast<int>((octave + 1) * 12 + (semitone % 12));
            if (midiNote < 0 || midiNote > 127)
                return std::nullopt;

            return midiNote;
        }

        std::optional<std::string> findSfzOpcodeValue(const std::string &content,
                                                      const std::string &opcodeLower)
        {
            const auto lowered = lowerAsciiCopy(content);
            size_t position = 0;

            while ((position = lowered.find(opcodeLower, position)) != std::string::npos)
            {
                const bool hasBoundaryBefore = position == 0 ||
                                               std::isspace(static_cast<unsigned char>(lowered[position - 1])) ||
                                               lowered[position - 1] == '<' ||
                                               lowered[position - 1] == '>';
                if (!hasBoundaryBefore)
                {
                    position += opcodeLower.length();
                    continue;
                }

                size_t cursor = position + opcodeLower.length();
                while (cursor < lowered.size() && std::isspace(static_cast<unsigned char>(lowered[cursor])))
                    ++cursor;

                if (cursor >= lowered.size() || lowered[cursor] != '=')
                {
                    position += opcodeLower.length();
                    continue;
                }

                ++cursor;
                while (cursor < content.size() && std::isspace(static_cast<unsigned char>(content[cursor])))
                    ++cursor;

                if (cursor >= content.size())
                    return std::nullopt;

                const char quote = (content[cursor] == '"' || content[cursor] == '\'') ? content[cursor] : '\0';
                if (quote != '\0')
                    ++cursor;

                size_t valueEnd = cursor;
                while (valueEnd < content.size())
                {
                    const char current = content[valueEnd];
                    if ((quote != '\0' && current == quote) ||
                        (quote == '\0' && (std::isspace(static_cast<unsigned char>(current)) || current == '<' || current == '>')))
                    {
                        break;
                    }

                    ++valueEnd;
                }

                if (valueEnd > cursor)
                    return content.substr(cursor, valueEnd - cursor);

                position += opcodeLower.length();
            }

            return std::nullopt;
        }

        std::vector<std::string> findSfzOpcodeValues(const std::string &content,
                                                     const std::string &opcodeLower)
        {
            const auto lowered = lowerAsciiCopy(content);
            size_t position = 0;
            std::vector<std::string> values;

            while ((position = lowered.find(opcodeLower, position)) != std::string::npos)
            {
                const bool hasBoundaryBefore = position == 0 ||
                                               std::isspace(static_cast<unsigned char>(lowered[position - 1])) ||
                                               lowered[position - 1] == '<' ||
                                               lowered[position - 1] == '>';
                if (!hasBoundaryBefore)
                {
                    position += opcodeLower.length();
                    continue;
                }

                size_t cursor = position + opcodeLower.length();
                while (cursor < lowered.size() && std::isspace(static_cast<unsigned char>(lowered[cursor])))
                    ++cursor;

                if (cursor >= lowered.size() || lowered[cursor] != '=')
                {
                    position += opcodeLower.length();
                    continue;
                }

                ++cursor;
                while (cursor < content.size() && std::isspace(static_cast<unsigned char>(content[cursor])))
                    ++cursor;

                if (cursor >= content.size())
                    break;

                const char quote = (content[cursor] == '"' || content[cursor] == '\'') ? content[cursor] : '\0';
                if (quote != '\0')
                    ++cursor;

                size_t valueEnd = cursor;
                while (valueEnd < content.size())
                {
                    const char current = content[valueEnd];
                    if ((quote != '\0' && current == quote) ||
                        (quote == '\0' && (std::isspace(static_cast<unsigned char>(current)) || current == '<' || current == '>')))
                    {
                        break;
                    }

                    ++valueEnd;
                }

                if (valueEnd > cursor)
                    values.push_back(content.substr(cursor, valueEnd - cursor));

                position = valueEnd;
            }

            return values;
        }

        struct IndexedInstrumentInfo
        {
            std::string samplePath;
            std::optional<int> rootMidiNote;
            juce::MemoryBlock embeddedSampleData;
            std::optional<std::string> presetName;
            std::optional<int> zoneCount;
            std::optional<int> keyLow;
            std::optional<int> keyHigh;
            std::optional<int> velocityLow;
            std::optional<int> velocityHigh;
        };

        void accumulateIndexedZone(IndexedInstrumentInfo &info,
                                   const std::string &samplePath,
                                   const std::optional<int> &rootMidiNote,
                                   std::optional<int> keyLow,
                                   std::optional<int> keyHigh,
                                   std::optional<int> velocityLow,
                                   std::optional<int> velocityHigh,
                                   const juce::MemoryBlock *embeddedSampleData = nullptr)
        {
            info.zoneCount = info.zoneCount.value_or(0) + 1;

            const auto resolvedKeyLow = keyLow.has_value() ? keyLow : (keyHigh.has_value() ? keyHigh : rootMidiNote);
            const auto resolvedKeyHigh = keyHigh.has_value() ? keyHigh : (keyLow.has_value() ? keyLow : rootMidiNote);
            if (resolvedKeyLow.has_value())
                updateOptionalMin(info.keyLow, *resolvedKeyLow);
            if (resolvedKeyHigh.has_value())
                updateOptionalMax(info.keyHigh, *resolvedKeyHigh);

            const auto resolvedVelocityLow = velocityLow.has_value() ? velocityLow : velocityHigh;
            const auto resolvedVelocityHigh = velocityHigh.has_value() ? velocityHigh : velocityLow;
            if (resolvedVelocityLow.has_value())
                updateOptionalMin(info.velocityLow, *resolvedVelocityLow);
            if (resolvedVelocityHigh.has_value())
                updateOptionalMax(info.velocityHigh, *resolvedVelocityHigh);

            const bool alreadyHasRepresentative = !info.samplePath.empty() || info.embeddedSampleData.getSize() > 0;
            if (!alreadyHasRepresentative)
            {
                if (embeddedSampleData != nullptr && embeddedSampleData->getSize() > 0)
                    info.embeddedSampleData = *embeddedSampleData;
                else
                    info.samplePath = samplePath;

                if (rootMidiNote.has_value())
                    info.rootMidiNote = rootMidiNote;
            }

            if (!info.rootMidiNote.has_value() && rootMidiNote.has_value())
                info.rootMidiNote = rootMidiNote;
        }

        std::optional<IndexedInstrumentInfo> readSfzInstrumentInfo(const std::filesystem::path &path)
        {
            std::ifstream in(path);
            if (!in)
                return std::nullopt;

            std::string content;
            std::string line;
            while (std::getline(in, line))
            {
                if (const auto commentPos = line.find("//"); commentPos != std::string::npos)
                    line.erase(commentPos);

                content.append(line);
                content.push_back('\n');
            }

            IndexedInstrumentInfo info;
            const auto defaultPath = findSfzOpcodeValue(content, "default_path");
            const auto samplePaths = findSfzOpcodeValues(content, "sample");
            const auto pitchKeyCenters = findSfzOpcodeValues(content, "pitch_keycenter");
            const auto keyValues = findSfzOpcodeValues(content, "key");
            const auto lowKeyValues = findSfzOpcodeValues(content, "lokey");
            const auto highKeyValues = findSfzOpcodeValues(content, "hikey");
            const auto lowVelocityValues = findSfzOpcodeValues(content, "lovel");
            const auto highVelocityValues = findSfzOpcodeValues(content, "hivel");

            auto resolveSamplePath = [&defaultPath](const std::string &sampleValue)
            {
                auto resolvedPath = std::filesystem::path(trimAsciiCopy(sampleValue));
                if (!resolvedPath.is_absolute())
                {
                    if (defaultPath.has_value())
                    {
                        const auto trimmedDefaultPath = trimAsciiCopy(*defaultPath);
                        if (!trimmedDefaultPath.empty())
                            resolvedPath = std::filesystem::path(trimmedDefaultPath) / resolvedPath;
                    }
                }

                return resolvedPath.generic_string();
            };

            for (size_t zoneIndex = 0; zoneIndex < samplePaths.size(); ++zoneIndex)
            {
                const auto rootNote = zoneIndex < pitchKeyCenters.size()
                                          ? parseMidiNoteValue(pitchKeyCenters[zoneIndex])
                                          : (zoneIndex < keyValues.size() ? parseMidiNoteValue(keyValues[zoneIndex]) : std::nullopt);
                const auto lowKey = zoneIndex < lowKeyValues.size()
                                        ? parseMidiNoteValue(lowKeyValues[zoneIndex])
                                        : std::nullopt;
                const auto highKey = zoneIndex < highKeyValues.size()
                                         ? parseMidiNoteValue(highKeyValues[zoneIndex])
                                         : std::nullopt;
                const auto lowVelocity = zoneIndex < lowVelocityValues.size()
                                             ? parseIntegerValue(lowVelocityValues[zoneIndex], 0, 127)
                                             : std::nullopt;
                const auto highVelocity = zoneIndex < highVelocityValues.size()
                                              ? parseIntegerValue(highVelocityValues[zoneIndex], 0, 127)
                                              : std::nullopt;

                accumulateIndexedZone(info,
                                      resolveSamplePath(samplePaths[zoneIndex]),
                                      rootNote,
                                      lowKey,
                                      highKey,
                                      lowVelocity,
                                      highVelocity);
            }

            if (!info.rootMidiNote.has_value())
            {
                if (const auto rootNote = findSfzOpcodeValue(content, "pitch_keycenter"); rootNote.has_value())
                    info.rootMidiNote = parseMidiNoteValue(*rootNote);
            }

            if (!info.rootMidiNote.has_value())
            {
                if (const auto rootNote = findSfzOpcodeValue(content, "key"); rootNote.has_value())
                    info.rootMidiNote = parseMidiNoteValue(*rootNote);
            }

            if (info.samplePath.empty() && info.embeddedSampleData.getSize() == 0 && !info.rootMidiNote.has_value() && !info.zoneCount.has_value())
                return std::nullopt;

            return info;
        }

        const juce::XmlElement *findFirstSampleElement(const juce::XmlElement &element)
        {
            forEachXmlChildElement(element, child)
            {
                if (child->getTagName().equalsIgnoreCase("sample"))
                    return child;

                if (const auto *nested = findFirstSampleElement(*child))
                    return nested;
            }

            return nullptr;
        }

        std::optional<IndexedInstrumentInfo> readDecentSamplerInstrumentInfo(const std::filesystem::path &path)
        {
            auto xml = juce::parseXML(juce::File(path.string()));
            if (xml == nullptr)
                return std::nullopt;

            IndexedInstrumentInfo info;
            if (const auto presetName = readAttributeIgnoreCase(*xml, {"presetName", "name"}); presetName.isNotEmpty())
                info.presetName = presetName.toStdString();

            std::vector<const juce::XmlElement *> sampleElements;
            collectXmlElements(*xml, {"sample"}, sampleElements);
            for (const auto *sample : sampleElements)
            {
                accumulateIndexedZone(info,
                                      readAttributeIgnoreCase(*sample, {"path", "sample", "file", "filename", "url"}).toStdString(),
                                      readMidiAttributeIgnoreCase(*sample, {"rootNote", "root", "rootKey"}),
                                      readMidiAttributeIgnoreCase(*sample, {"loNote", "lokey", "lowKey", "low"}),
                                      readMidiAttributeIgnoreCase(*sample, {"hiNote", "hikey", "highKey", "high"}),
                                      readIntegerAttributeIgnoreCase(*sample, {"loVel", "lovel", "lowVelocity"}, 0, 127),
                                      readIntegerAttributeIgnoreCase(*sample, {"hiVel", "hivel", "highVelocity"}, 0, 127));
            }

            if (info.samplePath.empty() && !info.rootMidiNote.has_value() && !info.zoneCount.has_value())
                return std::nullopt;

            return info;
        }

        std::optional<IndexedInstrumentInfo> readTalSamplerInstrumentInfo(const std::filesystem::path &path)
        {
            auto xml = juce::parseXML(juce::File(path.string()));
            if (xml == nullptr)
                return std::nullopt;

            IndexedInstrumentInfo info;
            if (const auto *program = findFirstXmlElement(*xml, {"program"}); program != nullptr)
            {
                if (const auto presetName = readAttributeIgnoreCase(*program, {"name", "programName"}); presetName.isNotEmpty())
                    info.presetName = presetName.toStdString();
            }

            std::vector<const juce::XmlElement *> sampleElements;
            collectXmlElements(*xml, {"sample"}, sampleElements);
            for (const auto *sample : sampleElements)
            {
                accumulateIndexedZone(info,
                                      readAttributeIgnoreCase(*sample, {"url", "path", "file", "filename", "samplepath"}).toStdString(),
                                      readMidiAttributeIgnoreCase(*sample, {"rootkey", "rootnote", "root"}),
                                      readMidiAttributeIgnoreCase(*sample, {"lokey", "lowkey", "low", "loNote"}),
                                      readMidiAttributeIgnoreCase(*sample, {"hikey", "highkey", "high", "hiNote"}),
                                      readIntegerAttributeIgnoreCase(*sample, {"lovel", "lowVelocity", "loVel"}, 0, 127),
                                      readIntegerAttributeIgnoreCase(*sample, {"hivel", "highVelocity", "hiVel"}, 0, 127));
            }

            if (info.samplePath.empty() && !info.rootMidiNote.has_value() && !info.zoneCount.has_value())
                return std::nullopt;

            return info;
        }

        std::optional<IndexedInstrumentInfo> readTx16WxInstrumentInfo(const std::filesystem::path &path)
        {
            auto xml = juce::parseXML(juce::File(path.string()));
            if (xml == nullptr)
                return std::nullopt;

            IndexedInstrumentInfo info;
            if (const auto *program = findFirstXmlElement(*xml, {"program"}); program != nullptr)
            {
                if (const auto presetName = readAttributeIgnoreCase(*program, {"name", "programName"}); presetName.isNotEmpty())
                    info.presetName = presetName.toStdString();
            }

            std::vector<const juce::XmlElement *> regions;
            collectXmlElements(*xml, {"region"}, regions);
            for (const auto *region : regions)
            {
                accumulateIndexedZone(info,
                                      readAttributeIgnoreCase(*region, {"sample", "file", "path", "filename"}).toStdString(),
                                      readMidiAttributeIgnoreCase(*region, {"rootkey", "root", "rootnote", "key"}),
                                      readMidiAttributeIgnoreCase(*region, {"lokey", "lowkey", "low", "loNote"}),
                                      readMidiAttributeIgnoreCase(*region, {"hikey", "highkey", "high", "hiNote"}),
                                      readIntegerAttributeIgnoreCase(*region, {"lovel", "lowVelocity", "loVel"}, 0, 127),
                                      readIntegerAttributeIgnoreCase(*region, {"hivel", "highVelocity", "hiVel"}, 0, 127));
            }

            if (info.samplePath.empty() && !info.rootMidiNote.has_value() && !info.zoneCount.has_value())
                return std::nullopt;

            return info;
        }

        std::optional<IndexedInstrumentInfo> readBitwigMultisampleInstrumentInfo(const std::filesystem::path &path)
        {
            juce::ZipFile zip(juce::File(path.string()));
            if (zip.getNumEntries() == 0)
                return std::nullopt;

            int manifestIndex = -1;
            for (int index = 0; index < zip.getNumEntries(); ++index)
            {
                const auto *entry = zip.getEntry(index);
                if (entry == nullptr)
                    continue;

                if (juce::File::createFileWithoutCheckingPath(entry->filename).getFileName().equalsIgnoreCase("multisample.xml"))
                {
                    manifestIndex = index;
                    break;
                }
            }

            if (manifestIndex < 0)
                return std::nullopt;

            std::unique_ptr<juce::InputStream> manifestStream(zip.createStreamForEntry(manifestIndex));
            if (manifestStream == nullptr)
                return std::nullopt;

            auto xml = juce::parseXML(manifestStream->readEntireStreamAsString());
            if (xml == nullptr || !xml->hasTagName("multisample"))
                return std::nullopt;

            IndexedInstrumentInfo info;
            if (const auto presetName = readAttributeIgnoreCase(*xml, {"name", "multisampleName"}); presetName.isNotEmpty())
                info.presetName = presetName.toStdString();

            std::vector<const juce::XmlElement *> sampleElements;
            collectXmlElements(*xml, {"sample"}, sampleElements);
            for (const auto *sample : sampleElements)
            {
                const auto samplePath = readAttributeIgnoreCase(*sample, {"file", "path", "sample", "filename"}).toStdString();
                const auto *keyNode = findFirstXmlElement(*sample, {"key"});
                const auto rootNote = keyNode != nullptr
                                          ? readMidiAttributeIgnoreCase(*keyNode, {"root", "rootkey", "rootnote"})
                                          : readMidiAttributeIgnoreCase(*sample, {"root", "rootkey", "rootnote"});
                const auto lowKey = keyNode != nullptr
                                        ? readMidiAttributeIgnoreCase(*keyNode, {"low", "lokey", "lowkey"})
                                        : readMidiAttributeIgnoreCase(*sample, {"low", "lokey", "lowkey"});
                const auto highKey = keyNode != nullptr
                                         ? readMidiAttributeIgnoreCase(*keyNode, {"high", "hikey", "highkey"})
                                         : readMidiAttributeIgnoreCase(*sample, {"high", "hikey", "highkey"});
                const auto lowVelocity = keyNode != nullptr
                                             ? readIntegerAttributeIgnoreCase(*keyNode, {"lowVelocity", "lovel", "lowvel"}, 0, 127)
                                             : readIntegerAttributeIgnoreCase(*sample, {"lowVelocity", "lovel", "lowvel"}, 0, 127);
                const auto highVelocity = keyNode != nullptr
                                              ? readIntegerAttributeIgnoreCase(*keyNode, {"highVelocity", "hivel", "highvel"}, 0, 127)
                                              : readIntegerAttributeIgnoreCase(*sample, {"highVelocity", "hivel", "highvel"}, 0, 127);

                juce::MemoryBlock embeddedSampleData;
                if (info.samplePath.empty() && info.embeddedSampleData.getSize() == 0)
                {
                    const int sampleEntryIndex = findZipEntryIndexByName(zip, juce::String(samplePath));
                    if (sampleEntryIndex >= 0)
                    {
                        if (std::unique_ptr<juce::InputStream> sampleStream(zip.createStreamForEntry(sampleEntryIndex)); sampleStream != nullptr)
                            sampleStream->readIntoMemoryBlock(embeddedSampleData);
                    }
                }

                accumulateIndexedZone(info,
                                      samplePath,
                                      rootNote,
                                      lowKey,
                                      highKey,
                                      lowVelocity,
                                      highVelocity,
                                      embeddedSampleData.getSize() > 0 ? &embeddedSampleData : nullptr);
            }

            if (info.samplePath.empty() && info.embeddedSampleData.getSize() == 0 && !info.rootMidiNote.has_value() && !info.zoneCount.has_value())
                return std::nullopt;

            return info;
        }

        std::optional<IndexedInstrumentInfo> readKorgMultisampleInstrumentInfo(const std::filesystem::path &path)
        {
            juce::ZipFile zip(juce::File(path.string()));
            if (zip.getNumEntries() == 0)
                return std::nullopt;

            std::unique_ptr<juce::XmlElement> xml;
            for (int index = 0; index < zip.getNumEntries(); ++index)
            {
                const auto *entry = zip.getEntry(index);
                if (entry == nullptr || !entry->filename.endsWithIgnoreCase(".xml"))
                    continue;

                if (std::unique_ptr<juce::InputStream> manifestStream(zip.createStreamForEntry(index)); manifestStream != nullptr)
                {
                    auto candidateXml = juce::parseXML(manifestStream->readEntireStreamAsString());
                    if (candidateXml != nullptr && (candidateXml->hasTagNameIgnoringNamespace("multisample") || findFirstXmlElement(*candidateXml, {"sample"}) != nullptr))
                    {
                        xml = std::move(candidateXml);
                        break;
                    }
                }
            }

            if (xml == nullptr)
                return std::nullopt;

            IndexedInstrumentInfo info;
            if (const auto presetName = readAttributeIgnoreCase(*xml, {"name", "multisampleName", "programName"}); presetName.isNotEmpty())
                info.presetName = presetName.toStdString();

            std::vector<const juce::XmlElement *> sampleElements;
            collectXmlElements(*xml, {"sample"}, sampleElements);
            for (const auto *sample : sampleElements)
            {
                const auto samplePath = readAttributeIgnoreCase(*sample, {"file", "path", "sample", "filename"}).toStdString();
                const auto rootNote = readMidiAttributeIgnoreCase(*sample, {"rootkey", "root", "rootnote"});
                const auto lowKey = readMidiAttributeIgnoreCase(*sample, {"lokey", "lowkey", "low", "loNote"});
                const auto highKey = readMidiAttributeIgnoreCase(*sample, {"hikey", "highkey", "high", "hiNote"});
                const auto lowVelocity = readIntegerAttributeIgnoreCase(*sample, {"lovel", "lowVelocity", "loVel"}, 0, 127);
                const auto highVelocity = readIntegerAttributeIgnoreCase(*sample, {"hivel", "highVelocity", "hiVel"}, 0, 127);

                juce::MemoryBlock embeddedSampleData;
                if (info.samplePath.empty() && info.embeddedSampleData.getSize() == 0)
                {
                    const int sampleEntryIndex = findZipEntryIndexByName(zip, juce::String(samplePath));
                    if (sampleEntryIndex >= 0)
                    {
                        if (std::unique_ptr<juce::InputStream> sampleStream(zip.createStreamForEntry(sampleEntryIndex)); sampleStream != nullptr)
                            sampleStream->readIntoMemoryBlock(embeddedSampleData);
                    }
                }

                accumulateIndexedZone(info,
                                      samplePath,
                                      rootNote,
                                      lowKey,
                                      highKey,
                                      lowVelocity,
                                      highVelocity,
                                      embeddedSampleData.getSize() > 0 ? &embeddedSampleData : nullptr);
            }

            if (info.samplePath.empty() && info.embeddedSampleData.getSize() == 0 && !info.rootMidiNote.has_value() && !info.zoneCount.has_value())
                return std::nullopt;

            return info;
        }

        struct Sf2PresetHeader
        {
            std::string name;
            uint16_t bagIndex = 0;
        };

        struct Sf2Bag
        {
            uint16_t genIndex = 0;
        };

        struct Sf2Generator
        {
            uint16_t oper = 0;
            uint16_t amount = 0;
        };

        struct Sf2InstrumentHeader
        {
            std::string name;
            uint16_t bagIndex = 0;
        };

        struct Sf2SampleHeader
        {
            std::string name;
            uint32_t start = 0;
            uint32_t end = 0;
            uint32_t startLoop = 0;
            uint32_t endLoop = 0;
            uint32_t sampleRate = 0;
            uint8_t originalPitch = 60;
        };

        struct Sf2LinkedZone
        {
            int linkIndex = -1;
            int keyLow = 0;
            int keyHigh = 127;
            int velocityLow = 0;
            int velocityHigh = 127;
            bool hasKeyRange = false;
            bool hasVelocityRange = false;
            std::optional<int> rootMidiNote;
        };

        struct Sf2IndexedInfo
        {
            std::string presetName;
            std::optional<int> rootMidiNote;
            std::optional<int> zoneCount;
            std::optional<int> keyLow;
            std::optional<int> keyHigh;
            std::optional<int> velocityLow;
            std::optional<int> velocityHigh;
            std::optional<int64_t> totalSamples;
            std::optional<int> sampleRate;
            std::optional<int> channels;
            std::optional<int> bitDepth;
            std::optional<int64_t> loopStartSample;
            std::optional<int64_t> loopEndSample;
            std::vector<float> overviewPeaks;
        };

        std::string readFixedAsciiString(const uint8_t *data, size_t size, size_t offset, size_t length)
        {
            if (offset + length > size)
                return {};

            size_t actualLength = 0;
            while (actualLength < length && data[offset + actualLength] != 0)
                ++actualLength;

            return trimAsciiCopy(std::string(reinterpret_cast<const char *>(data + offset), actualLength));
        }

        std::vector<Sf2PresetHeader> parseSf2PresetHeaders(const std::vector<uint8_t> &data)
        {
            constexpr size_t kRecordSize = 38;
            std::vector<Sf2PresetHeader> presets;
            for (size_t offset = 0; offset + kRecordSize <= data.size(); offset += kRecordSize)
            {
                Sf2PresetHeader header;
                header.name = readFixedAsciiString(data.data(), data.size(), offset, 20);
                if (const auto bagIndex = readU16LE(data.data(), data.size(), offset + 24); bagIndex.has_value())
                    header.bagIndex = *bagIndex;
                presets.push_back(std::move(header));
            }
            return presets;
        }

        std::vector<Sf2Bag> parseSf2Bags(const std::vector<uint8_t> &data)
        {
            constexpr size_t kRecordSize = 4;
            std::vector<Sf2Bag> bags;
            for (size_t offset = 0; offset + kRecordSize <= data.size(); offset += kRecordSize)
            {
                Sf2Bag bag;
                if (const auto genIndex = readU16LE(data.data(), data.size(), offset); genIndex.has_value())
                    bag.genIndex = *genIndex;
                bags.push_back(bag);
            }
            return bags;
        }

        std::vector<Sf2Generator> parseSf2Generators(const std::vector<uint8_t> &data)
        {
            constexpr size_t kRecordSize = 4;
            std::vector<Sf2Generator> generators;
            for (size_t offset = 0; offset + kRecordSize <= data.size(); offset += kRecordSize)
            {
                Sf2Generator generator;
                if (const auto oper = readU16LE(data.data(), data.size(), offset); oper.has_value())
                    generator.oper = *oper;
                if (const auto amount = readU16LE(data.data(), data.size(), offset + 2); amount.has_value())
                    generator.amount = *amount;
                generators.push_back(generator);
            }
            return generators;
        }

        std::vector<Sf2InstrumentHeader> parseSf2InstrumentHeaders(const std::vector<uint8_t> &data)
        {
            constexpr size_t kRecordSize = 22;
            std::vector<Sf2InstrumentHeader> instruments;
            for (size_t offset = 0; offset + kRecordSize <= data.size(); offset += kRecordSize)
            {
                Sf2InstrumentHeader instrument;
                instrument.name = readFixedAsciiString(data.data(), data.size(), offset, 20);
                if (const auto bagIndex = readU16LE(data.data(), data.size(), offset + 20); bagIndex.has_value())
                    instrument.bagIndex = *bagIndex;
                instruments.push_back(std::move(instrument));
            }
            return instruments;
        }

        std::vector<Sf2SampleHeader> parseSf2SampleHeaders(const std::vector<uint8_t> &data)
        {
            constexpr size_t kRecordSize = 46;
            std::vector<Sf2SampleHeader> samples;
            for (size_t offset = 0; offset + kRecordSize <= data.size(); offset += kRecordSize)
            {
                Sf2SampleHeader sample;
                sample.name = readFixedAsciiString(data.data(), data.size(), offset, 20);
                if (const auto start = readU32LE(data.data(), data.size(), offset + 20); start.has_value())
                    sample.start = *start;
                if (const auto end = readU32LE(data.data(), data.size(), offset + 24); end.has_value())
                    sample.end = *end;
                if (const auto startLoop = readU32LE(data.data(), data.size(), offset + 28); startLoop.has_value())
                    sample.startLoop = *startLoop;
                if (const auto endLoop = readU32LE(data.data(), data.size(), offset + 32); endLoop.has_value())
                    sample.endLoop = *endLoop;
                if (const auto sampleRate = readU32LE(data.data(), data.size(), offset + 36); sampleRate.has_value())
                    sample.sampleRate = *sampleRate;
                if (offset + 40 < data.size())
                    sample.originalPitch = data[offset + 40];
                samples.push_back(std::move(sample));
            }
            return samples;
        }

        std::vector<Sf2LinkedZone> parseSf2LinkedZones(const std::vector<Sf2Bag> &bags,
                                                       const std::vector<Sf2Generator> &generators,
                                                       size_t bagBegin,
                                                       size_t bagEndExclusive,
                                                       uint16_t linkOperator)
        {
            std::vector<Sf2LinkedZone> zones;
            Sf2LinkedZone globalDefaults;
            bool hasGlobalDefaults = false;

            if (bags.empty() || generators.empty() || bagBegin >= bagEndExclusive || bagEndExclusive > bags.size() - 1)
                return zones;

            for (size_t bagIndex = bagBegin; bagIndex < bagEndExclusive; ++bagIndex)
            {
                const size_t genBegin = bags[bagIndex].genIndex;
                const size_t genEnd = bags[bagIndex + 1].genIndex;
                if (genBegin > genEnd || genEnd > generators.size())
                    continue;

                Sf2LinkedZone zone;
                bool hasLink = false;

                for (size_t genIndex = genBegin; genIndex < genEnd; ++genIndex)
                {
                    const auto &generator = generators[genIndex];
                    switch (generator.oper)
                    {
                    case 41:
                    case 53:
                        if (generator.oper == linkOperator)
                        {
                            zone.linkIndex = generator.amount;
                            hasLink = true;
                        }
                        break;
                    case 43:
                        zone.keyLow = generator.amount & 0xFF;
                        zone.keyHigh = (generator.amount >> 8) & 0xFF;
                        zone.hasKeyRange = true;
                        break;
                    case 44:
                        zone.velocityLow = generator.amount & 0xFF;
                        zone.velocityHigh = (generator.amount >> 8) & 0xFF;
                        zone.hasVelocityRange = true;
                        break;
                    case 58:
                        zone.rootMidiNote = generator.amount & 0xFF;
                        break;
                    default:
                        break;
                    }
                }

                if (!hasLink)
                {
                    if (zones.empty())
                    {
                        globalDefaults = zone;
                        hasGlobalDefaults = true;
                    }
                    continue;
                }

                if (hasGlobalDefaults)
                {
                    if (!zone.hasKeyRange && globalDefaults.hasKeyRange)
                    {
                        zone.keyLow = globalDefaults.keyLow;
                        zone.keyHigh = globalDefaults.keyHigh;
                        zone.hasKeyRange = true;
                    }

                    if (!zone.hasVelocityRange && globalDefaults.hasVelocityRange)
                    {
                        zone.velocityLow = globalDefaults.velocityLow;
                        zone.velocityHigh = globalDefaults.velocityHigh;
                        zone.hasVelocityRange = true;
                    }

                    if (!zone.rootMidiNote.has_value() && globalDefaults.rootMidiNote.has_value())
                        zone.rootMidiNote = globalDefaults.rootMidiNote;
                }

                zones.push_back(zone);
            }

            return zones;
        }

        std::vector<float> buildOverviewPeaksFromSf2Sample(std::ifstream &in,
                                                           std::streampos smplDataOffset,
                                                           uint32_t sampleStart,
                                                           int64_t totalSamples,
                                                           int targetPeakCount)
        {
            if (totalSamples <= 0)
                return {};

            const int peakCount = static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(targetPeakCount, totalSamples)));
            const int64_t samplesPerPeak = std::max<int64_t>(1, totalSamples / peakCount);
            std::vector<float> peaks(static_cast<size_t>(peakCount), 0.0f);
            std::vector<int16_t> sampleBuffer;

            for (int peakIndex = 0; peakIndex < peakCount; ++peakIndex)
            {
                const int64_t zoneSampleStart = static_cast<int64_t>(sampleStart) + static_cast<int64_t>(peakIndex) * samplesPerPeak;
                const int64_t remaining = (static_cast<int64_t>(sampleStart) + totalSamples) - zoneSampleStart;
                if (remaining <= 0)
                    break;

                const int blockSamples = static_cast<int>(std::min<int64_t>(samplesPerPeak, remaining));
                sampleBuffer.assign(static_cast<size_t>(blockSamples), 0);

                in.clear();
                in.seekg(smplDataOffset + static_cast<std::streamoff>(zoneSampleStart * static_cast<int64_t>(sizeof(int16_t))), std::ios::beg);
                if (!in.good())
                    break;

                in.read(reinterpret_cast<char *>(sampleBuffer.data()), static_cast<std::streamsize>(blockSamples * static_cast<int>(sizeof(int16_t))));
                const auto samplesRead = static_cast<int>(in.gcount() / static_cast<std::streamsize>(sizeof(int16_t)));
                if (samplesRead <= 0)
                    break;

                int maxAbs = 0;
                for (int sampleIndex = 0; sampleIndex < samplesRead; ++sampleIndex)
                    maxAbs = std::max(maxAbs, std::abs(static_cast<int>(sampleBuffer[static_cast<size_t>(sampleIndex)])));

                peaks[static_cast<size_t>(peakIndex)] = static_cast<float>(maxAbs) / 32768.0f;
            }

            return peaks;
        }

        std::optional<Sf2IndexedInfo> readSf2IndexedInfo(const std::filesystem::path &path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return std::nullopt;

            std::array<char, 12> riffHeader{};
            in.read(riffHeader.data(), static_cast<std::streamsize>(riffHeader.size()));
            if (in.gcount() != static_cast<std::streamsize>(riffHeader.size()))
                return std::nullopt;

            if (std::memcmp(riffHeader.data(), "RIFF", 4) != 0 || std::memcmp(riffHeader.data() + 8, "sfbk", 4) != 0)
                return std::nullopt;

            std::vector<uint8_t> phdrData;
            std::vector<uint8_t> pbagData;
            std::vector<uint8_t> pgenData;
            std::vector<uint8_t> instData;
            std::vector<uint8_t> ibagData;
            std::vector<uint8_t> igenData;
            std::vector<uint8_t> shdrData;
            std::streampos smplDataOffset = 0;
            uint32_t smplDataSize = 0;

            while (in)
            {
                std::array<char, 8> chunkHeader{};
                in.read(chunkHeader.data(), static_cast<std::streamsize>(chunkHeader.size()));
                if (in.gcount() != static_cast<std::streamsize>(chunkHeader.size()))
                    break;

                const auto chunkSize = static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[4])) |
                                       (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[5])) << 8) |
                                       (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[6])) << 16) |
                                       (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[7])) << 24);
                const auto chunkDataStart = in.tellg();
                const auto chunkEnd = chunkDataStart + static_cast<std::streamoff>(chunkSize);

                if (std::memcmp(chunkHeader.data(), "LIST", 4) == 0 && chunkSize >= 4)
                {
                    std::array<char, 4> listType{};
                    in.read(listType.data(), static_cast<std::streamsize>(listType.size()));
                    if (in.gcount() != static_cast<std::streamsize>(listType.size()))
                        return std::nullopt;

                    if (std::memcmp(listType.data(), "sdta", 4) == 0 || std::memcmp(listType.data(), "pdta", 4) == 0)
                    {
                        while (in && in.tellg() < chunkEnd)
                        {
                            std::array<char, 8> subchunkHeader{};
                            in.read(subchunkHeader.data(), static_cast<std::streamsize>(subchunkHeader.size()));
                            if (in.gcount() != static_cast<std::streamsize>(subchunkHeader.size()))
                                break;

                            const auto subchunkSize = static_cast<uint32_t>(static_cast<uint8_t>(subchunkHeader[4])) |
                                                      (static_cast<uint32_t>(static_cast<uint8_t>(subchunkHeader[5])) << 8) |
                                                      (static_cast<uint32_t>(static_cast<uint8_t>(subchunkHeader[6])) << 16) |
                                                      (static_cast<uint32_t>(static_cast<uint8_t>(subchunkHeader[7])) << 24);
                            const auto subchunkDataStart = in.tellg();
                            const auto subchunkEnd = subchunkDataStart + static_cast<std::streamoff>(subchunkSize);

                            auto readSubchunkData = [&in, subchunkSize]()
                            {
                                std::vector<uint8_t> data(static_cast<size_t>(subchunkSize));
                                if (subchunkSize > 0)
                                    in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(subchunkSize));
                                return data;
                            };

                            if (std::memcmp(listType.data(), "sdta", 4) == 0 && std::memcmp(subchunkHeader.data(), "smpl", 4) == 0)
                            {
                                smplDataOffset = subchunkDataStart;
                                smplDataSize = subchunkSize;
                            }
                            else if (std::memcmp(listType.data(), "pdta", 4) == 0)
                            {
                                std::vector<uint8_t> data;
                                if (std::memcmp(subchunkHeader.data(), "phdr", 4) == 0)
                                    phdrData = readSubchunkData();
                                else if (std::memcmp(subchunkHeader.data(), "pbag", 4) == 0)
                                    pbagData = readSubchunkData();
                                else if (std::memcmp(subchunkHeader.data(), "pgen", 4) == 0)
                                    pgenData = readSubchunkData();
                                else if (std::memcmp(subchunkHeader.data(), "inst", 4) == 0)
                                    instData = readSubchunkData();
                                else if (std::memcmp(subchunkHeader.data(), "ibag", 4) == 0)
                                    ibagData = readSubchunkData();
                                else if (std::memcmp(subchunkHeader.data(), "igen", 4) == 0)
                                    igenData = readSubchunkData();
                                else if (std::memcmp(subchunkHeader.data(), "shdr", 4) == 0)
                                    shdrData = readSubchunkData();
                            }

                            in.clear();
                            in.seekg(subchunkEnd + static_cast<std::streamoff>(subchunkSize & 1u), std::ios::beg);
                        }
                    }
                }

                in.clear();
                in.seekg(chunkEnd + static_cast<std::streamoff>(chunkSize & 1u), std::ios::beg);
            }

            if (phdrData.empty() || pbagData.empty() || pgenData.empty() || instData.empty() || ibagData.empty() || igenData.empty() || shdrData.empty() || smplDataSize == 0)
                return std::nullopt;

            const auto presets = parseSf2PresetHeaders(phdrData);
            const auto presetBags = parseSf2Bags(pbagData);
            const auto presetGenerators = parseSf2Generators(pgenData);
            const auto instruments = parseSf2InstrumentHeaders(instData);
            const auto instrumentBags = parseSf2Bags(ibagData);
            const auto instrumentGenerators = parseSf2Generators(igenData);
            const auto samples = parseSf2SampleHeaders(shdrData);
            if (presets.size() < 2 || instruments.size() < 2 || samples.size() < 2 || presetBags.size() < 2 || instrumentBags.size() < 2)
                return std::nullopt;

            const auto &preset = presets.front();
            Sf2IndexedInfo info;
            info.presetName = preset.name.empty() ? path.stem().string() : preset.name;

            const auto presetZones = parseSf2LinkedZones(presetBags,
                                                         presetGenerators,
                                                         preset.bagIndex,
                                                         presets[1].bagIndex,
                                                         41);
            if (presetZones.empty())
                return std::nullopt;

            std::optional<Sf2SampleHeader> chosenSample;
            int zoneCount = 0;

            for (const auto &presetZone : presetZones)
            {
                if (presetZone.linkIndex < 0 || static_cast<size_t>(presetZone.linkIndex + 1) >= instruments.size())
                    continue;

                const auto &instrument = instruments[static_cast<size_t>(presetZone.linkIndex)];
                const auto instrumentZones = parseSf2LinkedZones(instrumentBags,
                                                                 instrumentGenerators,
                                                                 instrument.bagIndex,
                                                                 instruments[static_cast<size_t>(presetZone.linkIndex) + 1].bagIndex,
                                                                 53);
                for (const auto &instrumentZone : instrumentZones)
                {
                    if (instrumentZone.linkIndex < 0 || static_cast<size_t>(instrumentZone.linkIndex) >= samples.size() - 1)
                        continue;

                    const int lowKey = std::max(presetZone.keyLow, instrumentZone.keyLow);
                    const int highKey = std::min(presetZone.keyHigh, instrumentZone.keyHigh);
                    const int lowVelocity = std::max(presetZone.velocityLow, instrumentZone.velocityLow);
                    const int highVelocity = std::min(presetZone.velocityHigh, instrumentZone.velocityHigh);
                    if (highKey < lowKey || highVelocity < lowVelocity)
                        continue;

                    ++zoneCount;
                    updateOptionalMin(info.keyLow, lowKey);
                    updateOptionalMax(info.keyHigh, highKey);
                    updateOptionalMin(info.velocityLow, lowVelocity);
                    updateOptionalMax(info.velocityHigh, highVelocity);

                    const auto &sample = samples[static_cast<size_t>(instrumentZone.linkIndex)];
                    if (!info.rootMidiNote.has_value())
                        info.rootMidiNote = instrumentZone.rootMidiNote.value_or(static_cast<int>(sample.originalPitch));

                    if (!chosenSample.has_value())
                        chosenSample = sample;
                }
            }

            if (zoneCount <= 0 || !chosenSample.has_value())
                return std::nullopt;

            info.zoneCount = zoneCount;
            info.channels = 1;
            info.bitDepth = 16;

            const int64_t availableSamples = static_cast<int64_t>(smplDataSize / sizeof(int16_t));
            const int64_t sampleStart = std::min<int64_t>(chosenSample->start, availableSamples);
            const int64_t sampleEnd = std::min<int64_t>(chosenSample->end, availableSamples);
            if (sampleEnd <= sampleStart)
                return info;

            info.totalSamples = sampleEnd - sampleStart;
            info.sampleRate = static_cast<int>(chosenSample->sampleRate);
            if (chosenSample->startLoop > chosenSample->start && chosenSample->endLoop > chosenSample->startLoop)
            {
                info.loopStartSample = static_cast<int64_t>(chosenSample->startLoop) - static_cast<int64_t>(chosenSample->start);
                info.loopEndSample = std::min<int64_t>(static_cast<int64_t>(chosenSample->endLoop) - static_cast<int64_t>(chosenSample->start), *info.totalSamples - 1);
            }

            info.overviewPeaks = buildOverviewPeaksFromSf2Sample(in,
                                                                 smplDataOffset,
                                                                 chosenSample->start,
                                                                 *info.totalSamples,
                                                                 256);
            return info;
        }

        void populateIndexedInstrumentMetadata(const std::filesystem::path &presetPath,
                                               const std::string &presetCodecLabel,
                                               const std::optional<IndexedInstrumentInfo> &info,
                                               juce::AudioFormatManager &formatManager,
                                               WaveCacheBlobDb *waveCacheBlobDb,
                                               FileRecord &rec,
                                               std::vector<float> &overviewPeaks)
        {
            rec.codec = presetCodecLabel;

            if (!info.has_value())
                return;

            if (info->presetName.has_value() && !info->presetName->empty())
                rec.presetName = *info->presetName;

            rec.zoneCount = info->zoneCount;
            rec.keyLow = info->keyLow;
            rec.keyHigh = info->keyHigh;
            rec.velocityLow = info->velocityLow;
            rec.velocityHigh = info->velocityHigh;

            if (info->rootMidiNote.has_value())
            {
                const auto keyName = midiNoteName(*info->rootMidiNote);
                if (keyName.isNotEmpty())
                    rec.key = keyName.toStdString();
            }

            if (info->samplePath.empty())
            {
                if (info->embeddedSampleData.getSize() == 0)
                    return;
            }

            std::optional<int64_t> referencedSizeBytes;
            std::unique_ptr<juce::AudioFormatReader> reader;

            if (info->embeddedSampleData.getSize() > 0)
            {
                referencedSizeBytes = static_cast<int64_t>(info->embeddedSampleData.getSize());
                reader.reset(formatManager.createReaderFor(std::make_unique<juce::MemoryInputStream>(info->embeddedSampleData, false)));
            }
            else if (const auto resolvedSamplePath = resolveIndexedSamplePath(presetPath, info->samplePath); resolvedSamplePath.has_value())
            {
                std::error_code ec;
                const auto sampleSize = std::filesystem::file_size(*resolvedSamplePath, ec);
                if (!ec)
                    referencedSizeBytes = static_cast<int64_t>(sampleSize);

                reader.reset(formatManager.createReaderFor(juce::File(resolvedSamplePath->string())));
            }

            if (reader == nullptr)
                return;

            rec.totalSamples = static_cast<int64_t>(reader->lengthInSamples);
            rec.sampleRate = static_cast<int>(reader->sampleRate);
            rec.channels = static_cast<int>(reader->numChannels);
            rec.bitDepth = reader->bitsPerSample;
            rec.codec = presetCodecLabel + " -> " + reader->getFormatName().toStdString();

            if (reader->sampleRate > 0.0)
                rec.durationSec = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;

            if (referencedSizeBytes.has_value() && rec.durationSec.has_value() && *rec.durationSec > 0.0)
            {
                const auto kbps = static_cast<int>((static_cast<double>(*referencedSizeBytes) * 8.0) / (*rec.durationSec * 1000.0));
                rec.bitrateKbps = kbps;
            }

            if (waveCacheBlobDb != nullptr && waveCacheBlobDb->isOpen())
                overviewPeaks = buildOverviewPeaks(*reader, 256);
        }

        void populateSf2Metadata(const std::optional<Sf2IndexedInfo> &info,
                                 FileRecord &rec,
                                 std::vector<float> &overviewPeaks)
        {
            rec.codec = "SoundFont 2";

            if (!info.has_value())
                return;

            if (!info->presetName.empty())
                rec.presetName = info->presetName;

            rec.zoneCount = info->zoneCount;
            rec.keyLow = info->keyLow;
            rec.keyHigh = info->keyHigh;
            rec.velocityLow = info->velocityLow;
            rec.velocityHigh = info->velocityHigh;
            rec.totalSamples = info->totalSamples;
            rec.sampleRate = info->sampleRate;
            rec.channels = info->channels;
            rec.bitDepth = info->bitDepth;
            rec.loopStartSample = info->loopStartSample;
            rec.loopEndSample = info->loopEndSample;
            if (info->loopStartSample.has_value() && info->loopEndSample.has_value())
                rec.loopType = "sf2";

            if (info->rootMidiNote.has_value())
            {
                const auto keyName = midiNoteName(*info->rootMidiNote);
                if (keyName.isNotEmpty())
                    rec.key = keyName.toStdString();
            }

            if (rec.totalSamples.has_value() && rec.sampleRate.has_value() && *rec.sampleRate > 0)
                rec.durationSec = static_cast<double>(*rec.totalSamples) / static_cast<double>(*rec.sampleRate);

            if (rec.totalSamples.has_value() && rec.bitDepth.has_value() && rec.channels.has_value() && rec.durationSec.has_value() && *rec.durationSec > 0.0)
            {
                const auto audioBytes = static_cast<double>(*rec.totalSamples) * static_cast<double>(*rec.channels) * static_cast<double>(*rec.bitDepth) / 8.0;
                rec.bitrateKbps = static_cast<int>((audioBytes * 8.0) / (*rec.durationSec * 1000.0));
            }

            if (rec.bitDepth.has_value() && rec.channels.has_value())
                rec.codec = "SoundFont 2 -> PCM" + std::to_string(*rec.bitDepth) + " x" + std::to_string(*rec.channels);

            overviewPeaks = info->overviewPeaks;
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

            std::vector<int64_t> cueSampleOffsets;

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

                    std::optional<float> tempo;
                    if (chunkData.size() >= 16)
                        tempo = readF32LE(chunkData.data(), chunkData.size(), 12);
                    if ((!tempo.has_value() || *tempo <= 0.0f || *tempo > 400.0f) && chunkData.size() >= 24)
                        tempo = readF32LE(chunkData.data(), chunkData.size(), 20);

                    if (tempo.has_value())
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
                else if (std::memcmp(chunkId, "cue ", 4) == 0)
                {
                    const auto numCuePoints = readU32LE(chunkData.data(), chunkData.size(), 0);
                    if (!numCuePoints.has_value() || *numCuePoints == 0)
                        continue;

                    constexpr size_t cueHeaderSize = 4;
                    constexpr size_t cueEntrySize = 24;
                    const size_t availableEntries = (chunkData.size() > cueHeaderSize)
                                                        ? ((chunkData.size() - cueHeaderSize) / cueEntrySize)
                                                        : 0;
                    const size_t entriesToRead = std::min<size_t>(*numCuePoints, availableEntries);

                    for (size_t i = 0; i < entriesToRead; ++i)
                    {
                        const size_t entryOffset = cueHeaderSize + i * cueEntrySize;
                        const auto sampleOffset = readU32LE(chunkData.data(), chunkData.size(), entryOffset + 20);
                        if (sampleOffset.has_value())
                            cueSampleOffsets.push_back(static_cast<int64_t>(*sampleOffset));
                    }
                }
            }

            if ((!rec.loopStartSample.has_value() || !rec.loopEndSample.has_value()) && cueSampleOffsets.size() >= 2)
            {
                std::sort(cueSampleOffsets.begin(), cueSampleOffsets.end());
                rec.loopStartSample = cueSampleOffsets.front();
                rec.loopEndSample = cueSampleOffsets.back();
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

        void parseAiffAppleLoopMetadata(const std::filesystem::path &path, FileRecord &rec)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return;

            std::array<char, 12> formHeader{};
            in.read(formHeader.data(), static_cast<std::streamsize>(formHeader.size()));
            if (in.gcount() != static_cast<std::streamsize>(formHeader.size()))
                return;

            if (std::memcmp(formHeader.data(), "FORM", 4) != 0)
                return;

            const bool isAiff = std::memcmp(formHeader.data() + 8, "AIFF", 4) == 0 ||
                                std::memcmp(formHeader.data() + 8, "AIFC", 4) == 0;
            if (!isAiff)
                return;

            std::unordered_map<uint16_t, int64_t> markerIdToSample;
            std::optional<uint16_t> sustainLoopBeginMarkerId;
            std::optional<uint16_t> sustainLoopEndMarkerId;
            std::optional<int> rootMidiNote;
            bool hasAppleApplicationChunk = false;

            while (in)
            {
                std::array<char, 8> chunkHeader{};
                in.read(chunkHeader.data(), static_cast<std::streamsize>(chunkHeader.size()));
                if (in.gcount() != static_cast<std::streamsize>(chunkHeader.size()))
                    break;

                const char *chunkId = chunkHeader.data();
                const uint32_t chunkSize = (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[4])) << 24) |
                                           (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[5])) << 16) |
                                           (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[6])) << 8) |
                                           static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[7]));

                std::vector<uint8_t> chunkData(chunkSize);
                if (chunkSize > 0)
                {
                    in.read(reinterpret_cast<char *>(chunkData.data()), static_cast<std::streamsize>(chunkSize));
                    if (in.gcount() != static_cast<std::streamsize>(chunkSize))
                        break;
                }

                if ((chunkSize & 1u) != 0u)
                    in.seekg(1, std::ios::cur);

                if (std::memcmp(chunkId, "INST", 4) == 0)
                {
                    if (chunkData.size() >= 20)
                    {
                        rootMidiNote = static_cast<int>(chunkData[0]);

                        const auto sustainPlayMode = readU16BE(chunkData.data(), chunkData.size(), 8);
                        const auto beginMarkerId = readU16BE(chunkData.data(), chunkData.size(), 10);
                        const auto endMarkerId = readU16BE(chunkData.data(), chunkData.size(), 12);

                        if (sustainPlayMode.has_value() && *sustainPlayMode != 0 &&
                            beginMarkerId.has_value() && endMarkerId.has_value() &&
                            *beginMarkerId > 0 && *endMarkerId > 0)
                        {
                            sustainLoopBeginMarkerId = beginMarkerId;
                            sustainLoopEndMarkerId = endMarkerId;
                        }
                    }
                }
                else if (std::memcmp(chunkId, "MARK", 4) == 0)
                {
                    const auto markerCount = readU16BE(chunkData.data(), chunkData.size(), 0);
                    if (!markerCount.has_value() || *markerCount == 0)
                        continue;

                    size_t offset = 2;
                    for (uint16_t markerIndex = 0; markerIndex < *markerCount; ++markerIndex)
                    {
                        if (offset + 7 > chunkData.size())
                            break;

                        const auto markerId = readU16BE(chunkData.data(), chunkData.size(), offset);
                        const auto markerPosition = readU32BE(chunkData.data(), chunkData.size(), offset + 2);
                        if (!markerId.has_value() || !markerPosition.has_value())
                            break;

                        markerIdToSample[*markerId] = static_cast<int64_t>(*markerPosition);

                        const size_t markerNameLengthOffset = offset + 6;
                        const uint8_t markerNameLength = chunkData[markerNameLengthOffset];
                        size_t markerRecordSize = static_cast<size_t>(2 + 4 + 1 + markerNameLength);
                        if ((markerRecordSize & 1u) != 0u)
                            ++markerRecordSize;

                        offset += markerRecordSize;
                    }
                }
                else if (std::memcmp(chunkId, "APPL", 4) == 0)
                {
                    if (chunkData.size() >= 4)
                    {
                        if (std::memcmp(chunkData.data(), "stoc", 4) == 0 ||
                            std::memcmp(chunkData.data(), "AAPL", 4) == 0)
                        {
                            hasAppleApplicationChunk = true;
                        }
                    }
                }
            }

            if (rootMidiNote.has_value() && *rootMidiNote >= 0 && *rootMidiNote <= 127)
            {
                rec.acidRootNote = *rootMidiNote;
                if (!rec.key.has_value())
                {
                    const auto noteName = midiNoteName(*rootMidiNote);
                    if (noteName.isNotEmpty())
                        rec.key = noteName.toStdString();
                }
            }

            if (sustainLoopBeginMarkerId.has_value() && sustainLoopEndMarkerId.has_value())
            {
                const auto beginIt = markerIdToSample.find(*sustainLoopBeginMarkerId);
                const auto endIt = markerIdToSample.find(*sustainLoopEndMarkerId);
                if (beginIt != markerIdToSample.end() && endIt != markerIdToSample.end())
                {
                    const int64_t loopStart = std::min(beginIt->second, endIt->second);
                    const int64_t loopEnd = std::max(beginIt->second, endIt->second);
                    if (loopEnd > loopStart)
                    {
                        rec.loopStartSample = loopStart;
                        rec.loopEndSample = loopEnd;
                    }
                }
            }

            if (hasAppleApplicationChunk || rec.acidRootNote.has_value() || rec.loopStartSample.has_value())
                rec.loopType = std::string("apple-loop");
        }
    }

    Scanner::Scanner(CatalogDb &db, JobQueue &queue)
        : catalogDb(db), jobQueue(queue)
    {
    }

    void Scanner::setWaveCacheBlobDb(WaveCacheBlobDb *blobDb) noexcept
    {
        waveCacheBlobDb = blobDb;
    }

    bool Scanner::isPlayableExtension(const std::string &ext)
    {
        if (ext == "wav" || ext == "aif" || ext == "aiff" || ext == "flac" || ext == "mp3" || ext == "ogg" || ext == "acp")
            return true;

        // REX/RX2 files are playable when the REX SDK is available;
        // otherwise they fall back to index-only.
        if (isRexExtension(ext))
            return RexManager::isAvailable();

        return false;
    }

    bool Scanner::isIndexOnlyExtension(const std::string &ext)
    {
        // REX files without the SDK loaded are index-only.
        if (isRexExtension(ext))
            return !RexManager::isAvailable();

        if (ext == "sfz" || ext == "dspreset" || ext == "multisample" || ext == "talsmpl" || ext == "txprog" || ext == "sf2" || ext == "korgmultisample")
            return true;

        return false;
    }

    void Scanner::scanRoot(int64_t rootId,
                           const std::string &rootPath,
                           ProgressCallback onProgress,
                           CompletionCallback onCompleted)
    {
        struct ScanCandidate
        {
            fs::path absolutePath;
            std::string relativePath;
            std::string extension;
            bool playable = false;
            bool indexOnly = false;
        };

        struct ScanState
        {
            std::atomic<int> pendingJobs{0};
            std::atomic<bool> completionSignalled{false};
        };

        auto state = std::make_shared<ScanState>();

        const auto notifyCompletedOnce = [state, onCompleted]()
        {
            if (state->completionSignalled.exchange(true, std::memory_order_acq_rel))
                return;

            if (onCompleted)
                onCompleted();
        };

        const auto markJobFinished = [state, notifyCompletedOnce]()
        {
            const int previous = state->pendingJobs.fetch_sub(1, std::memory_order_acq_rel);
            if (previous == 1)
                notifyCompletedOnce();
        };

        state->pendingJobs.store(1, std::memory_order_release);

        jobQueue.enqueue(Job{
            [this, rootId, rootPath, onProgress, state, notifyCompletedOnce, markJobFinished](const std::atomic<uint64_t> &cancelGeneration, uint64_t jobGeneration)
            {
                const auto isCancelled = [&cancelGeneration, jobGeneration]()
                {
                    return cancelGeneration.load(std::memory_order_relaxed) != jobGeneration;
                };

                if (isCancelled())
                {
                    markJobFinished();
                    return;
                }

                std::vector<ScanCandidate> candidates;
                candidates.reserve(1024);

                std::error_code ec;
                for (auto it = fs::recursive_directory_iterator(rootPath,
                                                                fs::directory_options::skip_permission_denied, ec);
                     it != fs::recursive_directory_iterator(); ++it)
                {
                    if (isCancelled())
                    {
                        markJobFinished();
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

                    // Build relative path from source
                    auto relPath = fs::relative(path, rootPath, ec).generic_string();
                    if (ec)
                        continue;

                    candidates.push_back(ScanCandidate{
                        path,
                        std::move(relPath),
                        std::move(ext),
                        playable,
                        indexOnly});
                }

                if (candidates.empty())
                {
                    markJobFinished();
                    return;
                }

                thread_local juce::AudioFormatManager formatManager;
                thread_local bool formatsRegistered = false;
                if (!formatsRegistered)
                {
                    formatManager.registerBasicFormats();
                    formatsRegistered = true;
                }

                constexpr size_t kChunkSize = 32;
                for (size_t begin = 0; begin < candidates.size(); begin += kChunkSize)
                {
                    if (isCancelled())
                    {
                        markJobFinished();
                        return;
                    }

                    const size_t end = std::min(begin + kChunkSize, candidates.size());
                    if (!catalogDb.beginTransaction())
                    {
                        markJobFinished();
                        return;
                    }

                    bool transactionOpen = true;
                    for (size_t i = begin; i < end; ++i)
                    {
                        const auto &candidate = candidates[i];

                        if (isCancelled())
                        {
                            if (transactionOpen)
                                catalogDb.rollbackTransaction();
                            markJobFinished();
                            return;
                        }

                        FileRecord rec;
                        rec.rootId = rootId;
                        rec.relativePath = candidate.relativePath;
                        rec.filename = candidate.absolutePath.filename().string();
                        rec.extension = candidate.extension;
                        rec.indexOnly = candidate.indexOnly;

                        std::error_code fileEc;
                        rec.sizeBytes = static_cast<int64_t>(fs::file_size(candidate.absolutePath, fileEc));
                        auto ftime = fs::last_write_time(candidate.absolutePath, fileEc);
                        rec.modifiedTime = static_cast<int64_t>(
                            std::chrono::duration_cast<std::chrono::seconds>(
                                ftime.time_since_epoch())
                                .count());

                        if (!fileEc)
                            rec.contentHash = buildContentHash(candidate.absolutePath, rec.sizeBytes);

                        std::vector<float> overviewPeaks;

                        if (candidate.indexOnly && candidate.extension == "sfz")
                        {
                            populateIndexedInstrumentMetadata(candidate.absolutePath,
                                                              "SFZ",
                                                              readSfzInstrumentInfo(candidate.absolutePath),
                                                              formatManager,
                                                              waveCacheBlobDb,
                                                              rec,
                                                              overviewPeaks);
                        }
                        else if (candidate.indexOnly && candidate.extension == "dspreset")
                        {
                            populateIndexedInstrumentMetadata(candidate.absolutePath,
                                                              "DecentSampler",
                                                              readDecentSamplerInstrumentInfo(candidate.absolutePath),
                                                              formatManager,
                                                              waveCacheBlobDb,
                                                              rec,
                                                              overviewPeaks);
                        }
                        else if (candidate.indexOnly && candidate.extension == "multisample")
                        {
                            populateIndexedInstrumentMetadata(candidate.absolutePath,
                                                              "Bitwig Multisample",
                                                              readBitwigMultisampleInstrumentInfo(candidate.absolutePath),
                                                              formatManager,
                                                              waveCacheBlobDb,
                                                              rec,
                                                              overviewPeaks);
                        }
                        else if (candidate.indexOnly && candidate.extension == "talsmpl")
                        {
                            populateIndexedInstrumentMetadata(candidate.absolutePath,
                                                              "TAL Sampler",
                                                              readTalSamplerInstrumentInfo(candidate.absolutePath),
                                                              formatManager,
                                                              waveCacheBlobDb,
                                                              rec,
                                                              overviewPeaks);
                        }
                        else if (candidate.indexOnly && candidate.extension == "txprog")
                        {
                            populateIndexedInstrumentMetadata(candidate.absolutePath,
                                                              "TX16Wx",
                                                              readTx16WxInstrumentInfo(candidate.absolutePath),
                                                              formatManager,
                                                              waveCacheBlobDb,
                                                              rec,
                                                              overviewPeaks);
                        }
                        else if (candidate.indexOnly && candidate.extension == "korgmultisample")
                        {
                            populateIndexedInstrumentMetadata(candidate.absolutePath,
                                                              "Korg Multisample",
                                                              readKorgMultisampleInstrumentInfo(candidate.absolutePath),
                                                              formatManager,
                                                              waveCacheBlobDb,
                                                              rec,
                                                              overviewPeaks);
                        }
                        else if (candidate.indexOnly && candidate.extension == "sf2")
                        {
                            populateSf2Metadata(readSf2IndexedInfo(candidate.absolutePath),
                                                rec,
                                                overviewPeaks);
                        }
                        else if (candidate.playable && candidate.extension == "acp")
                        {
                            rec.codec = "Audiocity Preset";
                            rec.bitrateKbps = std::nullopt;

                            const juce::File presetFile(candidate.absolutePath.string());
                            if (auto preset = AcpPresetReader::readPreset(presetFile))
                            {
                                rec.acidRootNote = preset->rootMidiNote;
                                rec.loopStartSample = preset->loopStartSample;
                                rec.loopEndSample = preset->loopEndSample;
                                rec.bpm = preset->bpm;

                                const bool hasExplicitLoopRegion = preset->loopStartSample.has_value() &&
                                                                   preset->loopEndSample.has_value() &&
                                                                   *preset->loopEndSample > *preset->loopStartSample;
                                rec.loopType = (preset->loopPlayback || hasExplicitLoopRegion)
                                                   ? std::optional<std::string>{"audiocity-preset-loop"}
                                                   : std::optional<std::string>{"audiocity-preset"};

                                if (preset->sampleRate > 0.0)
                                    rec.sampleRate = static_cast<int>(std::round(preset->sampleRate));

                                if (preset->channels > 0)
                                    rec.channels = preset->channels;

                                rec.totalSamples = preset->totalSamples;
                                rec.durationSec = preset->durationSec;

                                if (preset->embeddedSampleBuffer != nullptr && preset->embeddedSampleBuffer->getNumSamples() > 0)
                                {
                                    rec.channels = preset->embeddedSampleBuffer->getNumChannels();
                                    rec.totalSamples = static_cast<int64_t>(preset->embeddedSampleBuffer->getNumSamples());
                                    rec.bitDepth = 32;
                                    if (rec.sampleRate.has_value() && *rec.sampleRate > 0)
                                        rec.durationSec = static_cast<double>(*rec.totalSamples) / static_cast<double>(*rec.sampleRate);

                                    if (waveCacheBlobDb != nullptr && waveCacheBlobDb->isOpen())
                                        overviewPeaks = buildOverviewPeaks(*preset->embeddedSampleBuffer, 256);
                                }
                                else
                                {
                                    const auto referencedSample = AcpPresetReader::resolveReferencedSampleFile(presetFile, preset->externalSamplePath);
                                    if (referencedSample.existsAsFile())
                                    {
                                        if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(referencedSample)))
                                        {
                                            rec.totalSamples = static_cast<int64_t>(reader->lengthInSamples);
                                            rec.sampleRate = static_cast<int>(reader->sampleRate);
                                            rec.channels = static_cast<int>(reader->numChannels);
                                            rec.bitDepth = reader->bitsPerSample;
                                            if (reader->sampleRate > 0.0)
                                                rec.durationSec = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;

                                            if (waveCacheBlobDb != nullptr && waveCacheBlobDb->isOpen())
                                                overviewPeaks = buildOverviewPeaks(*reader, 256);
                                        }
                                        else
                                        {
                                            rec.indexOnly = true;
                                        }
                                    }
                                    else
                                    {
                                        rec.indexOnly = true;
                                    }
                                }
                            }
                            else
                            {
                                rec.indexOnly = true;
                            }
                        }
                        else if (candidate.playable && isRexExtension(candidate.extension))
                        {
                            // --- REX / RX2: use the REX SDK for metadata ---
                            const auto absStr = candidate.absolutePath.string();
                            if (auto rexInfo = RexManager::readInfo(absStr))
                            {
                                rec.sampleRate = rexInfo->sampleRate;
                                rec.channels = rexInfo->channels;
                                rec.bitDepth = rexInfo->bitDepth;
                                rec.totalSamples = rexInfo->totalSamples;
                                rec.durationSec = rexInfo->durationSec;
                                rec.bpm = rexInfo->bpm;
                                rec.sliceCount = rexInfo->sliceCount;
                                rec.loopType = "rex";

                                rec.codec = (candidate.extension == "rx2") ? "REX2" : "REX";

                                if (rec.durationSec.has_value() && *rec.durationSec > 0.0 && rec.sizeBytes > 0)
                                {
                                    const auto kbps = static_cast<int>((static_cast<double>(rec.sizeBytes) * 8.0) / (*rec.durationSec * 1000.0));
                                    rec.bitrateKbps = kbps;
                                }

                                if (waveCacheBlobDb != nullptr && waveCacheBlobDb->isOpen())
                                {
                                    double decodedSampleRate = 0.0;
                                    if (auto decoded = RexManager::decodeToBuffer(absStr, decodedSampleRate))
                                        overviewPeaks = buildOverviewPeaks(*decoded, 256);
                                }
                            }
                        }
                        else if (candidate.playable)
                        {
                            if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(juce::File(candidate.absolutePath.string()))))
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

                                if (candidate.extension == "wav")
                                    parseWavAcidAndLoopMetadata(candidate.absolutePath, rec);
                                else if (candidate.extension == "aif" || candidate.extension == "aiff")
                                    parseAiffAppleLoopMetadata(candidate.absolutePath, rec);

                                if (waveCacheBlobDb != nullptr && waveCacheBlobDb->isOpen())
                                    overviewPeaks = buildOverviewPeaks(*reader, 256);
                            }
                        }

                        if (!catalogDb.upsertFile(rec))
                        {
                            if (transactionOpen)
                                catalogDb.rollbackTransaction();
                            markJobFinished();
                            return;
                        }

                        if (waveCacheBlobDb != nullptr && waveCacheBlobDb->isOpen() && !overviewPeaks.empty())
                        {
                            const auto cacheKey = buildWaveformCacheKey(rec.rootId,
                                                                        rec.relativePath,
                                                                        rec.sizeBytes,
                                                                        rec.modifiedTime);

                            if (const auto persisted = catalogDb.fileByRootAndRelativePath(rec.rootId, rec.relativePath); persisted.has_value())
                                waveCacheBlobDb->upsertPeaksByKey(cacheKey, persisted->id, overviewPeaks);
                        }

                        if (onProgress)
                            onProgress(candidate.relativePath);
                    }

                    if (!catalogDb.commitTransaction())
                    {
                        markJobFinished();
                        return;
                    }
                    transactionOpen = false;
                }

                markJobFinished();
            },
            JobPriority::Low});
    }

} // namespace sw
