#include "AcpPresetReader.h"

#include <cstring>

namespace sw
{

    namespace
    {
        juce::String stripWrappingQuotes(const juce::String &text)
        {
            auto trimmed = text.trim();
            if (trimmed.length() >= 2)
            {
                const auto first = trimmed[0];
                const auto last = trimmed[trimmed.length() - 1];
                if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
                    trimmed = trimmed.substring(1, trimmed.length() - 1).trim();
            }

            return trimmed;
        }

        bool isNumericString(const juce::String &text, const juce::String &allowedChars)
        {
            const auto trimmed = text.trim();
            return trimmed.isNotEmpty() && trimmed.retainCharacters(allowedChars) == trimmed;
        }

        std::optional<int> readOptionalIntProperty(const juce::ValueTree &tree, const char *propertyName)
        {
            const juce::Identifier property(propertyName);
            if (!tree.hasProperty(property))
                return std::nullopt;

            const auto text = stripWrappingQuotes(tree.getProperty(property).toString());
            if (!isNumericString(text, "+-0123456789"))
                return std::nullopt;

            return text.getIntValue();
        }

        std::optional<int64_t> readOptionalInt64Property(const juce::ValueTree &tree, const char *propertyName)
        {
            const juce::Identifier property(propertyName);
            if (!tree.hasProperty(property))
                return std::nullopt;

            const auto text = stripWrappingQuotes(tree.getProperty(property).toString());
            if (!isNumericString(text, "+-0123456789"))
                return std::nullopt;

            return static_cast<int64_t>(text.getLargeIntValue());
        }

        std::optional<double> readOptionalDoubleProperty(const juce::ValueTree &tree, const char *propertyName)
        {
            const juce::Identifier property(propertyName);
            if (!tree.hasProperty(property))
                return std::nullopt;

            const auto text = stripWrappingQuotes(tree.getProperty(property).toString());
            if (!isNumericString(text, "+-0123456789.eE"))
                return std::nullopt;

            return text.getDoubleValue();
        }

        juce::String readFirstStringProperty(const juce::ValueTree &tree,
                                             std::initializer_list<const char *> propertyNames)
        {
            for (const auto *propertyName : propertyNames)
            {
                const juce::Identifier property(propertyName);
                if (!tree.hasProperty(property))
                    continue;

                const auto text = stripWrappingQuotes(tree.getProperty(property).toString());
                if (text.isNotEmpty())
                    return text;
            }

            return {};
        }

        bool readPresetTree(const juce::File &presetFile, juce::ValueTree &tree)
        {
            juce::FileInputStream input(presetFile);
            if (input.openedOk())
            {
                tree = juce::ValueTree::readFromStream(input);
                if (tree.isValid())
                    return true;
            }

            if (auto xml = juce::parseXML(presetFile))
            {
                tree = juce::ValueTree::fromXml(*xml);
                if (tree.isValid())
                    return true;
            }

            return false;
        }

        std::shared_ptr<juce::AudioBuffer<float>> decodeEmbeddedSampleBuffer(const juce::String &base64Data,
                                                                             int storedChannels)
        {
            if (base64Data.isEmpty() || storedChannels <= 0)
                return {};

            juce::MemoryOutputStream decoded;
            if (!juce::Base64::convertFromBase64(decoded, base64Data))
                return {};

            const auto &block = decoded.getMemoryBlock();
            const auto frameStrideBytes = static_cast<size_t>(storedChannels) * sizeof(float);
            if (frameStrideBytes == 0 || (block.getSize() % frameStrideBytes) != 0)
                return {};

            const auto totalFrames = static_cast<int>(block.getSize() / frameStrideBytes);
            if (totalFrames <= 0)
                return {};

            const int outputChannels = juce::jlimit(1, 2, storedChannels);
            auto buffer = std::make_shared<juce::AudioBuffer<float>>(outputChannels, totalFrames);
            const auto *interleaved = static_cast<const float *>(block.getData());

            for (int frame = 0; frame < totalFrames; ++frame)
            {
                const auto frameOffset = static_cast<size_t>(frame) * static_cast<size_t>(storedChannels);
                for (int channel = 0; channel < outputChannels; ++channel)
                    buffer->setSample(channel, frame, interleaved[frameOffset + static_cast<size_t>(channel)]);
            }

            return buffer;
        }
    }

    std::optional<AcpPresetData> AcpPresetReader::readPreset(const juce::File &presetFile)
    {
        if (!presetFile.existsAsFile())
            return std::nullopt;

        juce::ValueTree tree;
        if (!readPresetTree(presetFile, tree))
            return std::nullopt;

        AcpPresetData preset;
        preset.presetName = readFirstStringProperty(tree, {"presetName", "embeddedSampleName", "name"});

        if (const auto rootNote = readOptionalIntProperty(tree, "rootMidiNote"))
            preset.rootMidiNote = *rootNote;
        else if (const auto embeddedRootNote = readOptionalIntProperty(tree, "embeddedSampleRootMidiNote"))
            preset.rootMidiNote = *embeddedRootNote;

        if (const auto embeddedSampleRate = readOptionalDoubleProperty(tree, "embeddedSampleRate"))
            preset.sampleRate = *embeddedSampleRate;
        else if (const auto presetSampleRate = readOptionalDoubleProperty(tree, "sampleRate"))
            preset.sampleRate = *presetSampleRate;
        else
            preset.sampleRate = 44100.0;

        if (const auto embeddedChannels = readOptionalIntProperty(tree, "embeddedSampleChannels"))
            preset.channels = *embeddedChannels;
        else if (const auto presetChannels = readOptionalIntProperty(tree, "channels"))
            preset.channels = *presetChannels;

        preset.loopStartSample = readOptionalInt64Property(tree, "loopStart");
        preset.loopEndSample = readOptionalInt64Property(tree, "loopEnd");
        preset.bpm = readOptionalDoubleProperty(tree, "bpm");

        const auto playbackMode = readFirstStringProperty(tree, {"playbackMode", "loopMode", "mode"});
        preset.loopPlayback = playbackMode.equalsIgnoreCase("loop") || playbackMode == "2";
        if (preset.loopStartSample.has_value() && preset.loopEndSample.has_value() &&
            *preset.loopEndSample > *preset.loopStartSample)
        {
            preset.loopPlayback = true;
        }

        preset.externalSamplePath = readFirstStringProperty(tree,
                                                            {"externalSamplePath",
                                                             "samplePath",
                                                             "sampleFilePath",
                                                             "sampleFile",
                                                             "sourceSamplePath",
                                                             "sourceFile",
                                                             "filePath",
                                                             "path"});

        const auto embeddedSampleData = readFirstStringProperty(tree,
                                                                {"embeddedSampleData",
                                                                 "embeddedAudioData",
                                                                 "sampleDataBase64"});
        preset.embeddedSampleBuffer = decodeEmbeddedSampleBuffer(embeddedSampleData, preset.channels);
        if (preset.embeddedSampleBuffer != nullptr)
        {
            preset.channels = preset.embeddedSampleBuffer->getNumChannels();
            preset.totalSamples = static_cast<int64_t>(preset.embeddedSampleBuffer->getNumSamples());
            if (preset.sampleRate <= 0.0)
                preset.sampleRate = 44100.0;

            preset.durationSec = static_cast<double>(*preset.totalSamples) / preset.sampleRate;
        }

        if (!preset.totalSamples.has_value())
            preset.totalSamples = readOptionalInt64Property(tree, "totalSamples");

        if (!preset.durationSec.has_value() && preset.totalSamples.has_value() && preset.sampleRate > 0.0)
            preset.durationSec = static_cast<double>(*preset.totalSamples) / preset.sampleRate;

        if (preset.channels <= 0)
            preset.channels = (preset.embeddedSampleBuffer != nullptr) ? preset.embeddedSampleBuffer->getNumChannels() : 1;

        return preset;
    }

    juce::File AcpPresetReader::resolveReferencedSampleFile(const juce::File &presetFile,
                                                            const juce::String &referencedPath)
    {
        const auto cleanedPath = stripWrappingQuotes(referencedPath);
        if (cleanedPath.isEmpty())
            return {};

        if (juce::File::isAbsolutePath(cleanedPath))
            return juce::File(cleanedPath);

        return presetFile.getParentDirectory().getChildFile(cleanedPath);
    }

} // namespace sw