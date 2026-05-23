#include "Pipeline/AcpPresetReader.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <filesystem>
#include <iostream>

namespace
{
    bool writeBinaryPreset(const juce::File &presetFile)
    {
        juce::ValueTree tree("AudiocityPatch");

        constexpr std::array<float, 4> sampleData{0.0f, 0.25f, -0.5f, 1.0f};
        tree.setProperty("embeddedSampleData",
                         juce::Base64::toBase64(sampleData.data(), sampleData.size() * sizeof(float)),
                         nullptr);
        tree.setProperty("embeddedSampleRate", 48000.0, nullptr);
        tree.setProperty("embeddedSampleChannels", 1, nullptr);
        tree.setProperty("rootMidiNote", 48, nullptr);
        tree.setProperty("loopStart", 1, nullptr);
        tree.setProperty("loopEnd", 3, nullptr);
        tree.setProperty("playbackMode", 2, nullptr);

        juce::FileOutputStream output(presetFile);
        if (!output.openedOk())
            return false;

        tree.writeToStream(output);
        output.flush();
        return output.getStatus().wasOk();
    }

    bool writeXmlPreset(const juce::File &presetFile)
    {
        juce::ValueTree tree("AudiocityPatch");
        tree.setProperty("samplePath", "Samples/External.wav", nullptr);
        tree.setProperty("rootMidiNote", 60, nullptr);

        return presetFile.replaceWithText(tree.toXmlString());
    }

    bool testEmbeddedBinaryPresetRead()
    {
        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("SampleWranglerAcpPresetReaderTests")
                                 .getNonexistentChildFile("embedded", "", false);
        tempDir.createDirectory();

        const auto presetFile = tempDir.getChildFile("embedded-test.acp");
        if (!writeBinaryPreset(presetFile))
        {
            tempDir.deleteRecursively();
            return false;
        }

        const auto preset = sw::AcpPresetReader::readPreset(presetFile);
        tempDir.deleteRecursively();

        if (!preset.has_value())
            return false;

        if (!preset->embeddedSampleBuffer || preset->embeddedSampleBuffer->getNumChannels() != 1)
            return false;

        if (preset->embeddedSampleBuffer->getNumSamples() != 4)
            return false;

        if (preset->rootMidiNote != 48 || !preset->loopPlayback)
            return false;

        if (!preset->loopStartSample.has_value() || !preset->loopEndSample.has_value())
            return false;

        if (*preset->loopStartSample != 1 || *preset->loopEndSample != 3)
            return false;

        return std::abs(preset->sampleRate - 48000.0) < 0.001;
    }

    bool testXmlPresetPathResolution()
    {
        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("SampleWranglerAcpPresetReaderTests")
                                 .getNonexistentChildFile("xml", "", false);
        tempDir.createDirectory();

        const auto samplesDir = tempDir.getChildFile("Samples");
        samplesDir.createDirectory();
        const auto externalFile = samplesDir.getChildFile("External.wav");
        if (!externalFile.replaceWithText("placeholder"))
        {
            tempDir.deleteRecursively();
            return false;
        }

        const auto presetFile = tempDir.getChildFile("external-test.acp");
        if (!writeXmlPreset(presetFile))
        {
            tempDir.deleteRecursively();
            return false;
        }

        const auto preset = sw::AcpPresetReader::readPreset(presetFile);
        if (!preset.has_value())
        {
            tempDir.deleteRecursively();
            return false;
        }

        const auto resolved = sw::AcpPresetReader::resolveReferencedSampleFile(presetFile, preset->externalSamplePath);
        const bool matches = resolved.existsAsFile() && resolved.getFullPathName() == externalFile.getFullPathName();
        tempDir.deleteRecursively();
        return matches;
    }
}

int main()
{
    if (!testEmbeddedBinaryPresetRead())
    {
        std::cerr << "testEmbeddedBinaryPresetRead failed.\n";
        return 1;
    }

    if (!testXmlPresetPathResolution())
    {
        std::cerr << "testXmlPresetPathResolution failed.\n";
        return 1;
    }

    return 0;
}