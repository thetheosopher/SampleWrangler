#include "ResultsPanel.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
    constexpr int kViewRecentId = 1;
    constexpr int kViewFavoritesId = 2;
    constexpr int kViewDuplicatesId = 3;
    constexpr int kSortNameAscId = 1;
    constexpr int kSortNameDescId = 2;
    constexpr int kSortNewestId = 3;
    constexpr int kSortOldestId = 4;
    constexpr int kSortSizeLargestId = 5;
    constexpr int kSortSizeSmallestId = 6;
    constexpr int kSortDurationShortestId = 7;
    constexpr int kSortDurationLongestId = 8;
    constexpr int kSortBpmSlowestId = 9;
    constexpr int kSortBpmFastestId = 10;
    constexpr int kSortKeyId = 11;
    constexpr int kFormatAnyId = 1;
    constexpr int kFormatAudioOnlyId = 2;
    constexpr int kFormatIndexedPresetId = 3;
    constexpr int kFormatWavId = 4;
    constexpr int kFormatAiffId = 5;
    constexpr int kFormatFlacId = 6;
    constexpr int kFormatMp3Id = 7;
    constexpr int kFormatOggId = 8;
    constexpr int kFormatRexId = 9;
    constexpr int kFormatSfzId = 10;
    constexpr int kFormatSf2Id = 11;
    constexpr int kChannelsAnyId = 1;
    constexpr int kChannelsMonoId = 2;
    constexpr int kChannelsStereoId = 3;
    constexpr int kChannelsMultiId = 4;
    constexpr int kLoopAnyId = 1;
    constexpr int kLoopLoopedOnlyId = 2;
    constexpr int kLoopOneShotOnlyId = 3;
    constexpr int kBpmAnyId = 1;
    constexpr int kBpmHasTempoId = 2;
    constexpr int kBpmBelow90Id = 3;
    constexpr int kBpm90To110Id = 4;
    constexpr int kBpm110To130Id = 5;
    constexpr int kBpm130To150Id = 6;
    constexpr int kBpm150UpId = 7;
    constexpr int kKeyAnyId = 1;
    constexpr int kKeyFirstPitchClassId = 2; // C = 2 ... B = 13
    constexpr int kWaveformColumnWidth = 120;
    constexpr int kSearchDebounceMs = 150;
    constexpr size_t kMaxCachedWaveformThumbnails = 2048;
    constexpr const char *kPitchClassNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    int selectorIdForBpmFilter(sw::ResultsPanel::BpmFilter filter)
    {
        switch (filter)
        {
        case sw::ResultsPanel::BpmFilter::HasTempo:
            return kBpmHasTempoId;
        case sw::ResultsPanel::BpmFilter::Below90:
            return kBpmBelow90Id;
        case sw::ResultsPanel::BpmFilter::From90To110:
            return kBpm90To110Id;
        case sw::ResultsPanel::BpmFilter::From110To130:
            return kBpm110To130Id;
        case sw::ResultsPanel::BpmFilter::From130To150:
            return kBpm130To150Id;
        case sw::ResultsPanel::BpmFilter::From150Up:
            return kBpm150UpId;
        case sw::ResultsPanel::BpmFilter::Any:
        default:
            return kBpmAnyId;
        }
    }

    sw::ResultsPanel::BpmFilter bpmFilterForSelectorId(int id)
    {
        switch (id)
        {
        case kBpmHasTempoId:
            return sw::ResultsPanel::BpmFilter::HasTempo;
        case kBpmBelow90Id:
            return sw::ResultsPanel::BpmFilter::Below90;
        case kBpm90To110Id:
            return sw::ResultsPanel::BpmFilter::From90To110;
        case kBpm110To130Id:
            return sw::ResultsPanel::BpmFilter::From110To130;
        case kBpm130To150Id:
            return sw::ResultsPanel::BpmFilter::From130To150;
        case kBpm150UpId:
            return sw::ResultsPanel::BpmFilter::From150Up;
        case kBpmAnyId:
        default:
            return sw::ResultsPanel::BpmFilter::Any;
        }
    }

    int selectorIdForKeyFilter(const std::optional<int> &pitchClass)
    {
        if (!pitchClass.has_value() || *pitchClass < 0 || *pitchClass > 11)
            return kKeyAnyId;
        return kKeyFirstPitchClassId + *pitchClass;
    }

    std::optional<int> keyFilterForSelectorId(int id)
    {
        if (id < kKeyFirstPitchClassId || id >= kKeyFirstPitchClassId + 12)
            return std::nullopt;
        return id - kKeyFirstPitchClassId;
    }

    int selectorIdForViewMode(sw::ResultsPanel::ViewMode mode)
    {
        switch (mode)
        {
        case sw::ResultsPanel::ViewMode::Duplicates:
            return kViewDuplicatesId;
        case sw::ResultsPanel::ViewMode::Favorites:
            return kViewFavoritesId;
        case sw::ResultsPanel::ViewMode::Recent:
        default:
            return kViewRecentId;
        }
    }

    sw::ResultsPanel::ViewMode viewModeForSelectorId(int id)
    {
        switch (id)
        {
        case kViewDuplicatesId:
            return sw::ResultsPanel::ViewMode::Duplicates;
        case kViewFavoritesId:
            return sw::ResultsPanel::ViewMode::Favorites;
        case kViewRecentId:
        default:
            return sw::ResultsPanel::ViewMode::Recent;
        }
    }

    int selectorIdForSortMode(sw::ResultsPanel::SortMode mode)
    {
        switch (mode)
        {
        case sw::ResultsPanel::SortMode::NameDesc:
            return kSortNameDescId;
        case sw::ResultsPanel::SortMode::NewestFirst:
            return kSortNewestId;
        case sw::ResultsPanel::SortMode::OldestFirst:
            return kSortOldestId;
        case sw::ResultsPanel::SortMode::SizeLargestFirst:
            return kSortSizeLargestId;
        case sw::ResultsPanel::SortMode::SizeSmallestFirst:
            return kSortSizeSmallestId;
        case sw::ResultsPanel::SortMode::DurationShortest:
            return kSortDurationShortestId;
        case sw::ResultsPanel::SortMode::DurationLongest:
            return kSortDurationLongestId;
        case sw::ResultsPanel::SortMode::BpmSlowest:
            return kSortBpmSlowestId;
        case sw::ResultsPanel::SortMode::BpmFastest:
            return kSortBpmFastestId;
        case sw::ResultsPanel::SortMode::KeyAsc:
            return kSortKeyId;
        case sw::ResultsPanel::SortMode::NameAsc:
        default:
            return kSortNameAscId;
        }
    }

    sw::ResultsPanel::SortMode sortModeForSelectorId(int id)
    {
        switch (id)
        {
        case kSortNameDescId:
            return sw::ResultsPanel::SortMode::NameDesc;
        case kSortNewestId:
            return sw::ResultsPanel::SortMode::NewestFirst;
        case kSortOldestId:
            return sw::ResultsPanel::SortMode::OldestFirst;
        case kSortSizeLargestId:
            return sw::ResultsPanel::SortMode::SizeLargestFirst;
        case kSortSizeSmallestId:
            return sw::ResultsPanel::SortMode::SizeSmallestFirst;
        case kSortDurationShortestId:
            return sw::ResultsPanel::SortMode::DurationShortest;
        case kSortDurationLongestId:
            return sw::ResultsPanel::SortMode::DurationLongest;
        case kSortBpmSlowestId:
            return sw::ResultsPanel::SortMode::BpmSlowest;
        case kSortBpmFastestId:
            return sw::ResultsPanel::SortMode::BpmFastest;
        case kSortKeyId:
            return sw::ResultsPanel::SortMode::KeyAsc;
        case kSortNameAscId:
        default:
            return sw::ResultsPanel::SortMode::NameAsc;
        }
    }

    int selectorIdForFormatFilter(sw::ResultsPanel::FormatFilter filter)
    {
        switch (filter)
        {
        case sw::ResultsPanel::FormatFilter::AudioOnly:
            return kFormatAudioOnlyId;
        case sw::ResultsPanel::FormatFilter::IndexedPresetOnly:
            return kFormatIndexedPresetId;
        case sw::ResultsPanel::FormatFilter::Wav:
            return kFormatWavId;
        case sw::ResultsPanel::FormatFilter::Aiff:
            return kFormatAiffId;
        case sw::ResultsPanel::FormatFilter::Flac:
            return kFormatFlacId;
        case sw::ResultsPanel::FormatFilter::Mp3:
            return kFormatMp3Id;
        case sw::ResultsPanel::FormatFilter::Ogg:
            return kFormatOggId;
        case sw::ResultsPanel::FormatFilter::Rex:
            return kFormatRexId;
        case sw::ResultsPanel::FormatFilter::Sfz:
            return kFormatSfzId;
        case sw::ResultsPanel::FormatFilter::Sf2:
            return kFormatSf2Id;
        case sw::ResultsPanel::FormatFilter::Any:
        default:
            return kFormatAnyId;
        }
    }

    sw::ResultsPanel::FormatFilter formatFilterForSelectorId(int id)
    {
        switch (id)
        {
        case kFormatAudioOnlyId:
            return sw::ResultsPanel::FormatFilter::AudioOnly;
        case kFormatIndexedPresetId:
            return sw::ResultsPanel::FormatFilter::IndexedPresetOnly;
        case kFormatWavId:
            return sw::ResultsPanel::FormatFilter::Wav;
        case kFormatAiffId:
            return sw::ResultsPanel::FormatFilter::Aiff;
        case kFormatFlacId:
            return sw::ResultsPanel::FormatFilter::Flac;
        case kFormatMp3Id:
            return sw::ResultsPanel::FormatFilter::Mp3;
        case kFormatOggId:
            return sw::ResultsPanel::FormatFilter::Ogg;
        case kFormatRexId:
            return sw::ResultsPanel::FormatFilter::Rex;
        case kFormatSfzId:
            return sw::ResultsPanel::FormatFilter::Sfz;
        case kFormatSf2Id:
            return sw::ResultsPanel::FormatFilter::Sf2;
        case kFormatAnyId:
        default:
            return sw::ResultsPanel::FormatFilter::Any;
        }
    }

    int selectorIdForChannelsFilter(sw::ResultsPanel::ChannelsFilter filter)
    {
        switch (filter)
        {
        case sw::ResultsPanel::ChannelsFilter::Mono:
            return kChannelsMonoId;
        case sw::ResultsPanel::ChannelsFilter::Stereo:
            return kChannelsStereoId;
        case sw::ResultsPanel::ChannelsFilter::MultiChannel:
            return kChannelsMultiId;
        case sw::ResultsPanel::ChannelsFilter::Any:
        default:
            return kChannelsAnyId;
        }
    }

    sw::ResultsPanel::ChannelsFilter channelsFilterForSelectorId(int id)
    {
        switch (id)
        {
        case kChannelsMonoId:
            return sw::ResultsPanel::ChannelsFilter::Mono;
        case kChannelsStereoId:
            return sw::ResultsPanel::ChannelsFilter::Stereo;
        case kChannelsMultiId:
            return sw::ResultsPanel::ChannelsFilter::MultiChannel;
        case kChannelsAnyId:
        default:
            return sw::ResultsPanel::ChannelsFilter::Any;
        }
    }

    int selectorIdForLoopFilter(sw::ResultsPanel::LoopFilter filter)
    {
        switch (filter)
        {
        case sw::ResultsPanel::LoopFilter::LoopedOnly:
            return kLoopLoopedOnlyId;
        case sw::ResultsPanel::LoopFilter::OneShotOnly:
            return kLoopOneShotOnlyId;
        case sw::ResultsPanel::LoopFilter::Any:
        default:
            return kLoopAnyId;
        }
    }

    sw::ResultsPanel::LoopFilter loopFilterForSelectorId(int id)
    {
        switch (id)
        {
        case kLoopLoopedOnlyId:
            return sw::ResultsPanel::LoopFilter::LoopedOnly;
        case kLoopOneShotOnlyId:
            return sw::ResultsPanel::LoopFilter::OneShotOnly;
        case kLoopAnyId:
        default:
            return sw::ResultsPanel::LoopFilter::Any;
        }
    }

    bool isAcidizedLoop(const sw::FileRecord &item)
    {
        return item.metadataSource.has_value() && *item.metadataSource == "acidized";
    }

    bool isAppleLoop(const sw::FileRecord &item)
    {
        return item.metadataSource.has_value() && *item.metadataSource == "apple-loop";
    }

    juce::String formatDuration(const std::optional<double> &durationSec)
    {
        if (!durationSec.has_value() || *durationSec < 0.0)
            return "--";

        const int totalMs = static_cast<int>(*durationSec * 1000.0);
        const int minutes = totalMs / 60000;
        const int seconds = (totalMs / 1000) % 60;
        const int ms = totalMs % 1000;
        return juce::String::formatted("%02d:%02d.%03d", minutes, seconds, ms);
    }

    juce::String midiNoteToName(int note)
    {
        static constexpr const char *kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        if (note < 0 || note > 127)
            return "--";

        const int octave = (note / 12) - 1;
        return juce::String(kNames[note % 12]) + juce::String(octave);
    }

    juce::String formatMidiRange(const std::optional<int> &low, const std::optional<int> &high)
    {
        if (!low.has_value() && !high.has_value())
            return "--";

        const int resolvedLow = low.value_or(*high);
        const int resolvedHigh = high.value_or(*low);
        if (resolvedLow == resolvedHigh)
            return midiNoteToName(resolvedLow);

        return midiNoteToName(resolvedLow) + "-" + midiNoteToName(resolvedHigh);
    }

    juce::String formatIntegerRange(const std::optional<int> &low, const std::optional<int> &high)
    {
        if (!low.has_value() && !high.has_value())
            return "--";

        const int resolvedLow = low.value_or(*high);
        const int resolvedHigh = high.value_or(*low);
        if (resolvedLow == resolvedHigh)
            return juce::String(resolvedLow);

        return juce::String(resolvedLow) + "-" + juce::String(resolvedHigh);
    }

    struct MetadataToken
    {
        juce::String text;
        bool isValue = false;
    };

    void appendMetadataPair(std::vector<MetadataToken> &tokens,
                            const juce::String &label,
                            const juce::String &value)
    {
        if (!tokens.empty())
            tokens.push_back({"  ", false});

        tokens.push_back({label, false});
        tokens.push_back({value, true});
    }

    std::vector<MetadataToken> buildMetadataTokens(const sw::FileRecord &item)
    {
        auto valueOrDash = [](const auto &opt, const juce::String &suffix = juce::String())
        {
            if (!opt.has_value())
                return juce::String("--");
            return juce::String(*opt) + suffix;
        };

        std::vector<MetadataToken> tokens;
        tokens.reserve(32);

        // Most browse-relevant details first so they survive truncation in narrow rows.
        if (item.duplicateClusterSize > 0)
            appendMetadataPair(tokens, "Exact cluster: ", juce::String(item.duplicateClusterSize));
        if (item.featureMatchScore.has_value())
            appendMetadataPair(tokens, "Feature-match heuristic: ",
                               juce::String(juce::roundToInt(*item.featureMatchScore)) + "%");

        appendMetadataPair(tokens, "Duration: ", formatDuration(item.durationSec));
        if (item.bpm.has_value())
            appendMetadataPair(tokens, "Tempo: ", juce::String(*item.bpm, 2));
        else if (item.inferredBpm.has_value())
            appendMetadataPair(tokens, "Tempo est.: ",
                               juce::String(*item.inferredBpm, 1) + " BPM (" +
                                   juce::String(juce::roundToInt(item.inferredBpmConfidence.value_or(0.0) * 100.0)) + "%)");
        if (item.key.has_value())
            appendMetadataPair(tokens, "Key: ", juce::String(*item.key));
        else if (item.inferredKey.has_value())
            appendMetadataPair(tokens, "Key est.: ",
                               juce::String(*item.inferredKey) + " (" +
                                   juce::String(juce::roundToInt(item.inferredKeyConfidence.value_or(0.0) * 100.0)) + "%)");

        const sw::LoopRegionRecord *loop = item.previewLoop();
        if (isAcidizedLoop(item) || isAppleLoop(item))
            appendMetadataPair(tokens, isAcidizedLoop(item) ? "Acid: " : "Apple Loop: ", "yes");
        if (item.rootMidiNote.has_value())
        {
            juce::String rootPitch = midiNoteToName(*item.rootMidiNote);
            if (item.rootTuneCents.has_value() && std::abs(*item.rootTuneCents) > 0.001)
                rootPitch += " " + juce::String::formatted("%+.2fc", *item.rootTuneCents);
            appendMetadataPair(tokens, "Root: ", rootPitch);
        }
        if (isAcidizedLoop(item))
            appendMetadataPair(tokens, "Beats: ", item.acidBeats.has_value() ? juce::String(*item.acidBeats) : juce::String("--"));
        if (!item.loopRegions.empty())
        {
            appendMetadataPair(tokens, "Loops: ", juce::String(static_cast<int>(item.loopRegions.size())));
            if (loop != nullptr)
            {
                const auto mode = loop->mode == sw::LoopMode::Forward
                                      ? "forward"
                                      : (loop->mode == sw::LoopMode::Alternating
                                             ? "alternating"
                                             : (loop->mode == sw::LoopMode::Backward ? "backward" : "unknown"));
                const auto endMarker = loop->endInclusive ? "]" : ")";
                appendMetadataPair(tokens,
                                   "Loop: ",
                                   juce::String(loop->startSample) + "-" + juce::String(loop->endSample) +
                                       endMarker + " " + mode);
            }
        }

        if (!item.presetPrograms.empty())
        {
            appendMetadataPair(tokens, "Programs: ", juce::String(static_cast<int>(item.presetPrograms.size())));

            juce::String programSummary;
            const auto visiblePrograms = std::min<size_t>(3, item.presetPrograms.size());
            for (size_t i = 0; i < visiblePrograms; ++i)
            {
                if (programSummary.isNotEmpty())
                    programSummary += ", ";
                const auto &program = item.presetPrograms[i];
                programSummary += juce::String(program.bank) + ":" + juce::String(program.program + 1);
                if (!program.name.empty())
                    programSummary += " " + juce::String(program.name);
            }
            if (item.presetPrograms.size() > visiblePrograms)
                programSummary += ", ...";
            appendMetadataPair(tokens, "Bank/Program: ", programSummary);
        }
        else if (item.presetName.has_value())
            appendMetadataPair(tokens, "Preset: ", juce::String(*item.presetName));

        if (item.zoneCount.has_value())
            appendMetadataPair(tokens, "Zones: ", juce::String(*item.zoneCount));

        if (item.keyLow.has_value() || item.keyHigh.has_value())
            appendMetadataPair(tokens, "Keys: ", formatMidiRange(item.keyLow, item.keyHigh));

        if (item.velocityLow.has_value() || item.velocityHigh.has_value())
            appendMetadataPair(tokens, "Vel: ", formatIntegerRange(item.velocityLow, item.velocityHigh));

        appendMetadataPair(tokens, "SR: ", valueOrDash(item.sampleRate, " Hz"));
        appendMetadataPair(tokens, "Ch: ", valueOrDash(item.channels));
        appendMetadataPair(tokens, "Bit Depth: ", valueOrDash(item.bitDepth));
        appendMetadataPair(tokens, "Bitrate: ", valueOrDash(item.bitrateKbps, " kbps"));
        appendMetadataPair(tokens, "Samples: ", item.totalSamples.has_value() ? juce::String(*item.totalSamples) : juce::String("--"));
        appendMetadataPair(tokens, "Codec/Enc: ", item.codec.has_value() ? juce::String(*item.codec) : juce::String("--"));

        if (item.inferredTransientCount.has_value())
            appendMetadataPair(tokens, "Transients est.: ", juce::String(*item.inferredTransientCount));
        if (item.loopRegions.empty() && item.suggestedLoopStartSample && item.suggestedLoopEndSample)
            appendMetadataPair(tokens, "Loop suggestion: ",
                               juce::String(*item.suggestedLoopStartSample) + "-" +
                                   juce::String(*item.suggestedLoopEndSample) + " (" +
                                   juce::String(juce::roundToInt(item.suggestedLoopConfidence.value_or(0.0) * 100.0)) + "%)");
        if (loop != nullptr && !loop->source.empty())
            appendMetadataPair(tokens, "Loop source: ", juce::String(loop->source));
        if (item.rootPitchSource.has_value())
            appendMetadataPair(tokens, "Pitch source: ", juce::String(*item.rootPitchSource));

        return tokens;
    }

    juce::String metadataTokensToText(const std::vector<MetadataToken> &tokens)
    {
        juce::String text;
        for (const auto &token : tokens)
            text += token.text;
        return text;
    }

    void drawMetadataTokens(juce::Graphics &g,
                            juce::Rectangle<int> bounds,
                            const std::vector<MetadataToken> &tokens,
                            juce::Colour labelColour,
                            juce::Colour valueColour)
    {
        g.saveState();
        g.reduceClipRegion(bounds);

        float x = static_cast<float>(bounds.getX());
        const float right = static_cast<float>(bounds.getRight());

        const auto measureTextWidth = [&g](const juce::String &text)
        {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText(g.getCurrentFont(), text, 0.0f, 0.0f);
            return glyphs.getBoundingBox(0, glyphs.getNumGlyphs(), true).getWidth();
        };

        for (const auto &token : tokens)
        {
            const float tokenWidth = measureTextWidth(token.text);
            if (x >= right)
                break;

            if (x + tokenWidth > right)
            {
                g.setColour(token.isValue ? valueColour : labelColour);
                g.drawFittedText(token.text,
                                 static_cast<int>(x),
                                 bounds.getY(),
                                 static_cast<int>(right - x),
                                 bounds.getHeight(),
                                 juce::Justification::centredLeft,
                                 1);
                break;
            }

            g.setColour(token.isValue ? valueColour : labelColour);
            g.drawText(token.text,
                       static_cast<int>(x),
                       bounds.getY(),
                       static_cast<int>(tokenWidth + 1.0f),
                       bounds.getHeight(),
                       juce::Justification::centredLeft,
                       false);
            x += tokenWidth;
        }

        g.restoreState();
    }
}

namespace sw
{

    int ResultsPanel::FacetFilters::activeCount() const noexcept
    {
        return (format != FormatFilter::Any ? 1 : 0) +
               (channels != ChannelsFilter::Any ? 1 : 0) +
               (loop != LoopFilter::Any ? 1 : 0) +
               (bpm != BpmFilter::Any ? 1 : 0) +
               (keyPitchClass.has_value() ? 1 : 0);
    }

    ResultsPanel::ResultsPanel()
    {
        searchBox.setTextToShowWhenEmpty("Search samples...  (Ctrl+F)", juce::Colours::grey);
        searchBox.setTooltip("Every word is matched as a prefix of words in the file name, path, or preset program names.");
        searchBox.onTextChange = [this]
        {
            if (suppressControlCallbacks)
                return;
            startTimer(kSearchDebounceMs);
        };
        searchBox.onReturnKey = [this]
        {
            fireSearchQueryChanged();
            focusResultsList();
        };
        searchBox.onEscapeKey = [this]
        {
            if (searchBox.getText().isNotEmpty())
            {
                suppressControlCallbacks = true;
                searchBox.clear();
                suppressControlCallbacks = false;
                fireSearchQueryChanged();
            }
            else if (nearDuplicateBannerVisible && onExitNearDuplicatesRequested)
            {
                onExitNearDuplicatesRequested();
            }
        };
        addAndMakeVisible(searchBox);

        viewSelector.addItem("All Files", kViewRecentId);
        viewSelector.addItem("Favorites", kViewFavoritesId);
        viewSelector.addItem("Duplicates", kViewDuplicatesId);
        viewSelector.setSelectedId(kViewRecentId, juce::dontSendNotification);
        viewSelector.setTooltip("Choose which files to browse");
        viewSelector.onChange = [this]
        {
            if (suppressControlCallbacks)
                return;

            viewMode = viewModeForSelectorId(viewSelector.getSelectedId());
            updateMetadataControlState();
            if (onViewModeChanged)
                onViewModeChanged(viewMode);
        };
        addAndMakeVisible(viewSelector);

        sortSelector.addItem("Name A-Z", kSortNameAscId);
        sortSelector.addItem("Name Z-A", kSortNameDescId);
        sortSelector.addItem("Date Newest", kSortNewestId);
        sortSelector.addItem("Date Oldest", kSortOldestId);
        sortSelector.addItem("Size Largest", kSortSizeLargestId);
        sortSelector.addItem("Size Smallest", kSortSizeSmallestId);
        sortSelector.addItem("Duration Shortest", kSortDurationShortestId);
        sortSelector.addItem("Duration Longest", kSortDurationLongestId);
        sortSelector.addItem("Tempo Slowest", kSortBpmSlowestId);
        sortSelector.addItem("Tempo Fastest", kSortBpmFastestId);
        sortSelector.addItem("Key (C to B)", kSortKeyId);
        sortSelector.setSelectedId(kSortNameAscId, juce::dontSendNotification);
        sortSelector.setTooltip("Sort results. Tempo and key use embedded metadata, falling back to analysis estimates; files without a value sort last.");
        sortSelector.onChange = [this]
        {
            if (suppressControlCallbacks)
                return;

            sortMode = sortModeForSelectorId(sortSelector.getSelectedId());
            reapplySortPreservingSelection();

            if (onSortModeChanged)
                onSortModeChanged(sortMode);
        };
        addAndMakeVisible(sortSelector);

        const auto facetChanged = [this]
        {
            if (suppressControlCallbacks)
                return;

            facetFilters.format = formatFilterForSelectorId(formatSelector.getSelectedId());
            facetFilters.channels = channelsFilterForSelectorId(channelsSelector.getSelectedId());
            facetFilters.loop = loopFilterForSelectorId(loopSelector.getSelectedId());
            facetFilters.bpm = bpmFilterForSelectorId(bpmSelector.getSelectedId());
            facetFilters.keyPitchClass = keyFilterForSelectorId(keySelector.getSelectedId());
            updateFilterControlState();

            if (onFacetFiltersChanged)
                onFacetFiltersChanged(facetFilters);
        };

        formatSelector.addItem("Format: Any", kFormatAnyId);
        formatSelector.addItem("Format: Audio", kFormatAudioOnlyId);
        formatSelector.addItem("Format: Indexed", kFormatIndexedPresetId);
        formatSelector.addItem("WAV", kFormatWavId);
        formatSelector.addItem("AIFF", kFormatAiffId);
        formatSelector.addItem("FLAC", kFormatFlacId);
        formatSelector.addItem("MP3", kFormatMp3Id);
        formatSelector.addItem("OGG", kFormatOggId);
        formatSelector.addItem("REX/RX2", kFormatRexId);
        formatSelector.addItem("SFZ", kFormatSfzId);
        formatSelector.addItem("SF2", kFormatSf2Id);
        formatSelector.setSelectedId(kFormatAnyId, juce::dontSendNotification);
        formatSelector.onChange = facetChanged;
        addAndMakeVisible(formatSelector);

        channelsSelector.addItem("Channels: Any", kChannelsAnyId);
        channelsSelector.addItem("Mono", kChannelsMonoId);
        channelsSelector.addItem("Stereo", kChannelsStereoId);
        channelsSelector.addItem("Multi", kChannelsMultiId);
        channelsSelector.setSelectedId(kChannelsAnyId, juce::dontSendNotification);
        channelsSelector.onChange = facetChanged;
        addAndMakeVisible(channelsSelector);

        loopSelector.addItem("Loop: Any", kLoopAnyId);
        loopSelector.addItem("Looped", kLoopLoopedOnlyId);
        loopSelector.addItem("One-shot", kLoopOneShotOnlyId);
        loopSelector.setSelectedId(kLoopAnyId, juce::dontSendNotification);
        loopSelector.onChange = facetChanged;
        addAndMakeVisible(loopSelector);

        bpmSelector.addItem("Tempo: Any", kBpmAnyId);
        bpmSelector.addItem("Has tempo", kBpmHasTempoId);
        bpmSelector.addItem("< 90 BPM", kBpmBelow90Id);
        bpmSelector.addItem("90-110 BPM", kBpm90To110Id);
        bpmSelector.addItem("110-130 BPM", kBpm110To130Id);
        bpmSelector.addItem("130-150 BPM", kBpm130To150Id);
        bpmSelector.addItem("150+ BPM", kBpm150UpId);
        bpmSelector.setSelectedId(kBpmAnyId, juce::dontSendNotification);
        bpmSelector.setTooltip("Filter by embedded tempo, falling back to the analysis estimate");
        bpmSelector.onChange = facetChanged;
        addAndMakeVisible(bpmSelector);

        keySelector.addItem("Key: Any", kKeyAnyId);
        for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
            keySelector.addItem(juce::String("Key: ") + kPitchClassNames[pitchClass], kKeyFirstPitchClassId + pitchClass);
        keySelector.setSelectedId(kKeyAnyId, juce::dontSendNotification);
        keySelector.setTooltip("Filter by key or root note pitch class (embedded first, then analysis estimate)");
        keySelector.onChange = facetChanged;
        addAndMakeVisible(keySelector);

        clearFiltersButton.setTooltip("Reset all filters to Any");
        clearFiltersButton.onClick = [this]
        {
            setFacetFilters({});
            if (onFacetFiltersChanged)
                onFacetFiltersChanged(facetFilters);
        };
        addChildComponent(clearFiltersButton);

        favoriteToggle.setTooltip("Mark the selected file as a favorite (F)");
        favoriteToggle.onClick = [this]
        {
            if (suppressControlCallbacks)
                return;

            if (onSelectedFileFavoriteChanged)
                onSelectedFileFavoriteChanged(favoriteToggle.getToggleState());
        };
        addAndMakeVisible(favoriteToggle);

        nearDuplicatesButton.setTooltip("List near-duplicate candidates for the selected file, ranked by a coarse feature-match heuristic. "
                                        "This does not confirm they are duplicates.");
        nearDuplicatesButton.onClick = [this]
        {
            if (const auto *selectedFile = getResultAt(getSelectedRow()); selectedFile != nullptr && onNearDuplicatesRequested)
                onNearDuplicatesRequested(*selectedFile);
        };
        addAndMakeVisible(nearDuplicatesButton);

        revealButton.setTooltip("Show the selected file in File Explorer (Ctrl+R)");
        revealButton.onClick = [this]
        {
            if (const auto *selectedFile = getResultAt(getSelectedRow()); selectedFile != nullptr && onRevealFileRequested)
                onRevealFileRequested(*selectedFile);
        };
        addAndMakeVisible(revealButton);

        nearDuplicateBannerLabel.setJustificationType(juce::Justification::centredLeft);
        nearDuplicateBannerLabel.setFont(juce::FontOptions(13.0f));
        addChildComponent(nearDuplicateBannerLabel);

        exitNearDuplicatesButton.setTooltip("Return to the normal results list (Esc)");
        exitNearDuplicatesButton.onClick = [this]
        {
            if (onExitNearDuplicatesRequested)
                onExitNearDuplicatesRequested();
        };
        addChildComponent(exitNearDuplicatesButton);

        resultsList.setModel(this);
        resultsList.setRowHeight(40);
        addAndMakeVisible(resultsList);

        emptyStateLabel.setJustificationType(juce::Justification::centred);
        emptyStateLabel.setFont(juce::FontOptions(15.0f));
        emptyStateLabel.setInterceptsMouseClicks(false, false);
        addChildComponent(emptyStateLabel);

        emptyStateActionButton.onClick = [this]
        {
            if (onEmptyStateActionRequested && emptyStateAction != EmptyStateAction::None)
                onEmptyStateActionRequested(emptyStateAction);
        };
        addChildComponent(emptyStateActionButton);

        setDarkMode(true);
        updateMetadataControlState();
        updateFilterControlState();
    }

    void ResultsPanel::paint(juce::Graphics &g)
    {
        g.fillAll(darkModeEnabled ? juce::Colour(0xff1e1e1e) : juce::Colour(0xfffafafa));

        if (nearDuplicateBannerVisible)
        {
            g.setColour(darkModeEnabled ? juce::Colour(0xff3a3320) : juce::Colour(0xfffff3cd));
            g.fillRoundedRectangle(nearDuplicateBannerLabel.getBounds().getUnion(exitNearDuplicatesButton.getBounds()).expanded(2).toFloat(), 4.0f);
        }
    }

    void ResultsPanel::resized()
    {
        constexpr int rowHeight = 28;
        constexpr int controlGap = 6;
        constexpr int viewWidth = 110;
        constexpr int sortWidth = 140;
        constexpr int favoriteWidth = 88;
        constexpr int nearDuplicatesWidth = 120;
        constexpr int revealWidth = 72;
        constexpr int minSearchWidth = 140;

        auto area = getLocalBounds().reduced(4);
        auto topRow = area.removeFromTop(rowHeight);
        area.removeFromTop(4);
        auto filterRow = area.removeFromTop(rowHeight);
        area.removeFromTop(4);

        viewSelector.setBounds(topRow.removeFromRight(viewWidth));
        topRow.removeFromRight(controlGap);
        sortSelector.setBounds(topRow.removeFromRight(sortWidth));
        topRow.removeFromRight(controlGap);

        // Selection actions share the search row; drop them before squeezing search below its minimum.
        const int actionsWidth = favoriteWidth + nearDuplicatesWidth + revealWidth + controlGap * 3;
        const bool showActions = topRow.getWidth() - actionsWidth >= minSearchWidth;
        favoriteToggle.setVisible(showActions);
        nearDuplicatesButton.setVisible(showActions);
        revealButton.setVisible(showActions);
        if (showActions)
        {
            revealButton.setBounds(topRow.removeFromRight(revealWidth));
            topRow.removeFromRight(controlGap);
            nearDuplicatesButton.setBounds(topRow.removeFromRight(nearDuplicatesWidth));
            topRow.removeFromRight(controlGap);
            favoriteToggle.setBounds(topRow.removeFromRight(favoriteWidth));
            topRow.removeFromRight(controlGap);
        }
        searchBox.setBounds(topRow);

        constexpr int clearWidth = 110;
        if (clearFiltersButton.isVisible())
        {
            clearFiltersButton.setBounds(filterRow.removeFromRight(clearWidth));
            filterRow.removeFromRight(controlGap);
        }

        juce::ComboBox *filterCombos[] = {&formatSelector, &channelsSelector, &loopSelector, &bpmSelector, &keySelector};
        constexpr int preferredWidths[] = {140, 125, 105, 120, 95};
        const int comboCount = static_cast<int>(std::size(filterCombos));
        const int preferredTotal = 140 + 125 + 105 + 120 + 95;
        const int available = juce::jmax(0, filterRow.getWidth() - controlGap * (comboCount - 1));
        const float scale = juce::jmin(1.0f, static_cast<float>(available) / static_cast<float>(preferredTotal));
        for (int i = 0; i < comboCount; ++i)
        {
            const int width = static_cast<int>(static_cast<float>(preferredWidths[i]) * scale);
            filterCombos[i]->setBounds(filterRow.removeFromLeft(width));
            filterRow.removeFromLeft(controlGap);
        }

        if (nearDuplicateBannerVisible)
        {
            auto bannerRow = area.removeFromTop(rowHeight).reduced(2, 0);
            area.removeFromTop(4);
            exitNearDuplicatesButton.setBounds(bannerRow.removeFromRight(130).reduced(0, 2));
            bannerRow.removeFromRight(controlGap);
            nearDuplicateBannerLabel.setBounds(bannerRow);
        }

        resultsList.setBounds(area);

        auto emptyArea = area.reduced(24).withSizeKeepingCentre(juce::jmin(area.getWidth() - 48, 520), 110);
        emptyStateLabel.setBounds(emptyArea.removeFromTop(64));
        emptyArea.removeFromTop(8);
        emptyStateActionButton.setBounds(emptyArea.withSizeKeepingCentre(160, 28));
    }

    void ResultsPanel::setResults(std::vector<FileRecord> newResults)
    {
        const auto *previouslySelected = getResultAt(resultsList.getSelectedRow());
        const int64_t previouslySelectedId = previouslySelected != nullptr ? previouslySelected->id : -1;

        // Rebuilding the list must not look like a new user selection (that would
        // reload/auto-play the preview), so selection callbacks are suppressed.
        suppressSelectionCallbacks = true;
        resultsList.deselectAllRows();
        waveformCacheMisses.clear();

        results = std::move(newResults);
        nearDuplicateResultsActive = std::any_of(results.begin(), results.end(), [](const FileRecord &file)
                                                 { return file.featureMatchScore.has_value(); });
        applySort();

        resultsList.updateContent();

        bool reselected = false;
        if (previouslySelectedId >= 0)
        {
            const auto it = std::find_if(results.begin(), results.end(), [previouslySelectedId](const FileRecord &file)
                                         { return file.id == previouslySelectedId; });
            if (it != results.end())
            {
                resultsList.selectRow(static_cast<int>(std::distance(results.begin(), it)));
                reselected = true;
            }
        }
        suppressSelectionCallbacks = false;

        if (!reselected && previouslySelectedId >= 0)
            setSelectedFileMetadata(std::nullopt);

        updateMetadataControlState();
        updateEmptyStateVisibility();
        repaint();
    }

    void ResultsPanel::setSearchQuery(const std::string &query)
    {
        stopTimer();
        suppressControlCallbacks = true;
        searchBox.setText(query, juce::dontSendNotification);
        suppressControlCallbacks = false;
    }

    std::string ResultsPanel::getSearchQuery() const
    {
        return searchBox.getText().toStdString();
    }

    void ResultsPanel::focusSearchBox()
    {
        searchBox.grabKeyboardFocus();
        searchBox.selectAll();
    }

    void ResultsPanel::focusResultsList()
    {
        if (!results.empty() && resultsList.getSelectedRow() < 0)
            resultsList.selectRow(0);
        resultsList.grabKeyboardFocus();
    }

    void ResultsPanel::fireSearchQueryChanged()
    {
        stopTimer();
        if (onSearchQueryChanged)
            onSearchQueryChanged(searchBox.getText().toStdString());
    }

    void ResultsPanel::timerCallback()
    {
        fireSearchQueryChanged();
    }

    void ResultsPanel::setNearDuplicateReference(std::optional<juce::String> referenceName)
    {
        const bool visible = referenceName.has_value();
        if (visible)
            nearDuplicateBannerLabel.setText("Near-duplicate candidates for: " + *referenceName +
                                                 "   (ranked by feature-match heuristic, not confirmed)",
                                             juce::dontSendNotification);

        if (visible == nearDuplicateBannerVisible)
            return;

        nearDuplicateBannerVisible = visible;
        nearDuplicateBannerLabel.setVisible(visible);
        exitNearDuplicatesButton.setVisible(visible);
        resized();
        repaint();
    }

    void ResultsPanel::setEmptyState(const juce::String &message, EmptyStateAction action)
    {
        emptyStateLabel.setText(message, juce::dontSendNotification);
        emptyStateAction = action;

        switch (action)
        {
        case EmptyStateAction::AddSource:
            emptyStateActionButton.setButtonText("Add Source Folder");
            break;
        case EmptyStateAction::ClearSearch:
            emptyStateActionButton.setButtonText("Clear Search");
            break;
        case EmptyStateAction::ClearFilters:
            emptyStateActionButton.setButtonText("Clear Filters");
            break;
        case EmptyStateAction::ShowAllFiles:
            emptyStateActionButton.setButtonText("Show All Files");
            break;
        case EmptyStateAction::ExitNearDuplicates:
            emptyStateActionButton.setButtonText("Back to Results");
            break;
        case EmptyStateAction::None:
        default:
            emptyStateActionButton.setButtonText({});
            break;
        }

        updateEmptyStateVisibility();
    }

    void ResultsPanel::updateEmptyStateVisibility()
    {
        const bool showEmptyState = results.empty() && emptyStateLabel.getText().isNotEmpty();
        emptyStateLabel.setVisible(showEmptyState);
        emptyStateActionButton.setVisible(showEmptyState && emptyStateAction != EmptyStateAction::None);
    }

    void ResultsPanel::setViewMode(ViewMode mode)
    {
        viewMode = mode;
        suppressControlCallbacks = true;
        viewSelector.setSelectedId(selectorIdForViewMode(mode), juce::dontSendNotification);
        suppressControlCallbacks = false;
        updateMetadataControlState();
    }

    ResultsPanel::ViewMode ResultsPanel::getViewMode() const noexcept
    {
        return viewMode;
    }

    void ResultsPanel::setSortMode(SortMode mode)
    {
        sortMode = mode;
        suppressControlCallbacks = true;
        sortSelector.setSelectedId(selectorIdForSortMode(mode), juce::dontSendNotification);
        suppressControlCallbacks = false;
        reapplySortPreservingSelection();
    }

    void ResultsPanel::reapplySortPreservingSelection()
    {
        const auto *selected = getResultAt(resultsList.getSelectedRow());
        const int64_t selectedId = selected != nullptr ? selected->id : -1;

        suppressSelectionCallbacks = true;
        applySort();
        resultsList.updateContent();
        if (selectedId >= 0)
        {
            const auto it = std::find_if(results.begin(), results.end(), [selectedId](const FileRecord &file)
                                         { return file.id == selectedId; });
            if (it != results.end())
                resultsList.selectRow(static_cast<int>(std::distance(results.begin(), it)));
        }
        suppressSelectionCallbacks = false;
        repaint();
    }

    ResultsPanel::SortMode ResultsPanel::getSortMode() const noexcept
    {
        return sortMode;
    }

    void ResultsPanel::setFacetFilters(FacetFilters filters)
    {
        facetFilters = filters;
        suppressControlCallbacks = true;
        formatSelector.setSelectedId(selectorIdForFormatFilter(facetFilters.format), juce::dontSendNotification);
        channelsSelector.setSelectedId(selectorIdForChannelsFilter(facetFilters.channels), juce::dontSendNotification);
        loopSelector.setSelectedId(selectorIdForLoopFilter(facetFilters.loop), juce::dontSendNotification);
        bpmSelector.setSelectedId(selectorIdForBpmFilter(facetFilters.bpm), juce::dontSendNotification);
        keySelector.setSelectedId(selectorIdForKeyFilter(facetFilters.keyPitchClass), juce::dontSendNotification);
        suppressControlCallbacks = false;
        updateFilterControlState();
    }

    ResultsPanel::FacetFilters ResultsPanel::getFacetFilters() const noexcept
    {
        return facetFilters;
    }

    void ResultsPanel::updateFilterControlState()
    {
        const int active = facetFilters.activeCount();
        clearFiltersButton.setButtonText("Clear Filters (" + juce::String(active) + ")");
        const bool wasVisible = clearFiltersButton.isVisible();
        clearFiltersButton.setVisible(active > 0);

        const auto accent = darkModeEnabled ? juce::Colour(0xff6b9bc8) : juce::Colour(0xff2f6fa8);
        const auto outline = darkModeEnabled ? juce::Colour(0xff4d4d4d) : juce::Colour(0xffb8b8b8);
        formatSelector.setColour(juce::ComboBox::outlineColourId, facetFilters.format != FormatFilter::Any ? accent : outline);
        channelsSelector.setColour(juce::ComboBox::outlineColourId, facetFilters.channels != ChannelsFilter::Any ? accent : outline);
        loopSelector.setColour(juce::ComboBox::outlineColourId, facetFilters.loop != LoopFilter::Any ? accent : outline);
        bpmSelector.setColour(juce::ComboBox::outlineColourId, facetFilters.bpm != BpmFilter::Any ? accent : outline);
        keySelector.setColour(juce::ComboBox::outlineColourId, facetFilters.keyPitchClass.has_value() ? accent : outline);

        if (wasVisible != clearFiltersButton.isVisible())
            resized();
    }

    void ResultsPanel::setSelectedFileMetadata(std::optional<FileUserDataRecord> userData)
    {
        suppressControlCallbacks = true;
        favoriteToggle.setToggleState(userData.has_value() && userData->isFavorite, juce::dontSendNotification);
        suppressControlCallbacks = false;
        updateMetadataControlState();
    }

    void ResultsPanel::applySort()
    {
        auto compareNameAsc = [](const FileRecord &a, const FileRecord &b)
        {
            const auto nameCmp = juce::String(a.filename).compareIgnoreCase(juce::String(b.filename));
            if (nameCmp != 0)
                return nameCmp < 0;

            const auto relCmp = juce::String(a.relativePath).compareIgnoreCase(juce::String(b.relativePath));
            if (relCmp != 0)
                return relCmp < 0;

            return a.id < b.id;
        };

        auto compareNameDesc = [compareNameAsc](const FileRecord &a, const FileRecord &b)
        {
            if (compareNameAsc(a, b))
                return false;
            if (compareNameAsc(b, a))
                return true;
            return false;
        };

        auto compareNewestFirst = [compareNameAsc](const FileRecord &a, const FileRecord &b)
        {
            if (a.modifiedTime != b.modifiedTime)
                return a.modifiedTime > b.modifiedTime;

            return compareNameAsc(a, b);
        };

        auto compareOldestFirst = [compareNameAsc](const FileRecord &a, const FileRecord &b)
        {
            if (a.modifiedTime != b.modifiedTime)
                return a.modifiedTime < b.modifiedTime;

            return compareNameAsc(a, b);
        };

        auto compareSizeLargestFirst = [compareNameAsc](const FileRecord &a, const FileRecord &b)
        {
            if (a.sizeBytes != b.sizeBytes)
                return a.sizeBytes > b.sizeBytes;

            return compareNameAsc(a, b);
        };

        auto compareSizeSmallestFirst = [compareNameAsc](const FileRecord &a, const FileRecord &b)
        {
            if (a.sizeBytes != b.sizeBytes)
                return a.sizeBytes < b.sizeBytes;

            return compareNameAsc(a, b);
        };

        // Files missing the sort value always go last, regardless of direction.
        const auto compareOptional = [compareNameAsc](const auto &aValue, const auto &bValue, bool ascending,
                                                      const FileRecord &a, const FileRecord &b)
        {
            if (aValue.has_value() != bValue.has_value())
                return aValue.has_value();
            if (aValue.has_value() && *aValue != *bValue)
                return ascending ? (*aValue < *bValue) : (*aValue > *bValue);
            return compareNameAsc(a, b);
        };

        auto compareDurationShortest = [compareOptional](const FileRecord &a, const FileRecord &b)
        { return compareOptional(a.durationSec, b.durationSec, true, a, b); };
        auto compareDurationLongest = [compareOptional](const FileRecord &a, const FileRecord &b)
        { return compareOptional(a.durationSec, b.durationSec, false, a, b); };
        auto compareBpmSlowest = [compareOptional](const FileRecord &a, const FileRecord &b)
        { return compareOptional(a.effectiveBpm(), b.effectiveBpm(), true, a, b); };
        auto compareBpmFastest = [compareOptional](const FileRecord &a, const FileRecord &b)
        { return compareOptional(a.effectiveBpm(), b.effectiveBpm(), false, a, b); };
        auto compareKey = [compareOptional](const FileRecord &a, const FileRecord &b)
        {
            const auto pitchClassOf = [](const FileRecord &file) -> std::optional<int>
            {
                const auto &keyText = file.effectiveKey();
                return keyText.has_value() ? keyPitchClass(*keyText) : std::nullopt;
            };
            return compareOptional(pitchClassOf(a), pitchClassOf(b), true, a, b);
        };

        if (nearDuplicateResultsActive)
        {
            std::stable_sort(results.begin(), results.end(), [compareNameAsc](const FileRecord &a, const FileRecord &b)
                             {
                                 if (a.featureMatchScore != b.featureMatchScore)
                                     return a.featureMatchScore.value_or(0.0) > b.featureMatchScore.value_or(0.0);
                                 return compareNameAsc(a, b);
                             });
            return;
        }

        const auto sortWithDuplicateGroups = [this](const auto &comparator)
        {
            std::sort(results.begin(), results.end(), [this, &comparator](const FileRecord &a, const FileRecord &b)
                      {
                          if (viewMode == ViewMode::Duplicates && a.contentHash != b.contentHash)
                              return a.contentHash.value_or("") < b.contentHash.value_or("");
                          return comparator(a, b);
                      });
        };

        switch (sortMode)
        {
        case SortMode::NameDesc:
            sortWithDuplicateGroups(compareNameDesc);
            break;
        case SortMode::NewestFirst:
            sortWithDuplicateGroups(compareNewestFirst);
            break;
        case SortMode::OldestFirst:
            sortWithDuplicateGroups(compareOldestFirst);
            break;
        case SortMode::SizeLargestFirst:
            sortWithDuplicateGroups(compareSizeLargestFirst);
            break;
        case SortMode::SizeSmallestFirst:
            sortWithDuplicateGroups(compareSizeSmallestFirst);
            break;
        case SortMode::DurationShortest:
            sortWithDuplicateGroups(compareDurationShortest);
            break;
        case SortMode::DurationLongest:
            sortWithDuplicateGroups(compareDurationLongest);
            break;
        case SortMode::BpmSlowest:
            sortWithDuplicateGroups(compareBpmSlowest);
            break;
        case SortMode::BpmFastest:
            sortWithDuplicateGroups(compareBpmFastest);
            break;
        case SortMode::KeyAsc:
            sortWithDuplicateGroups(compareKey);
            break;
        case SortMode::NameAsc:
        default:
            sortWithDuplicateGroups(compareNameAsc);
            break;
        }
    }

    const std::vector<float> *ResultsPanel::loadWaveformPeaksForFile(const FileRecord &file)
    {
        if (auto existing = waveformPeaksByFileId.find(file.id); existing != waveformPeaksByFileId.end())
            return &existing->second;

        if (waveformCacheMisses.find(file.id) != waveformCacheMisses.end())
            return nullptr;

        if (onResolveWaveformCachePeaksForFile != nullptr)
        {
            if (const auto resolved = onResolveWaveformCachePeaksForFile(file); resolved.has_value() && !resolved->empty())
            {
                if (waveformPeaksByFileId.size() >= kMaxCachedWaveformThumbnails && !waveformPeaksInsertionOrder.empty())
                {
                    waveformPeaksByFileId.erase(waveformPeaksInsertionOrder.front());
                    waveformPeaksInsertionOrder.pop_front();
                }

                waveformPeaksInsertionOrder.push_back(file.id);
                return &(waveformPeaksByFileId[file.id] = *resolved);
            }
        }

        waveformCacheMisses.insert(file.id);
        return nullptr;
    }

    void ResultsPanel::paintWaveformPreview(juce::Graphics &g, juce::Rectangle<int> bounds, const FileRecord &item)
    {
        const auto backgroundColour = darkModeEnabled ? juce::Colour(0xff1b2434) : juce::Colour(0xffdfe9f9);
        const auto outlineColour = darkModeEnabled ? juce::Colour(0xff3b4a61) : juce::Colour(0xffa8bfdc);
        const auto waveformColour = darkModeEnabled ? juce::Colour(0xff66e0ff) : juce::Colour(0xff1769aa);
        const auto placeholderColour = darkModeEnabled ? juce::Colour(0xff7b8da6) : juce::Colour(0xff7087a1);

        g.setColour(backgroundColour);
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

        g.setColour(outlineColour);
        g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 1.0f);

        const auto *peaks = loadWaveformPeaksForFile(item);
        if (peaks == nullptr || peaks->empty())
        {
            g.setColour(placeholderColour);
            g.setFont(10.0f);
            g.drawFittedText("No cache", bounds.reduced(4), juce::Justification::centred, 1);
            return;
        }

        const auto content = bounds.reduced(3, 3);
        const float centreY = static_cast<float>(content.getCentreY());
        const float halfHeight = static_cast<float>(content.getHeight()) * 0.48f;
        const int width = juce::jmax(1, content.getWidth());
        const int peakCount = static_cast<int>(peaks->size());
        const float widthScale = (width > 1) ? static_cast<float>(width - 1) : 1.0f;

        g.setColour(waveformColour.withAlpha(0.18f));
        g.drawHorizontalLine(static_cast<int>(std::round(centreY)), static_cast<float>(content.getX()), static_cast<float>(content.getRight()));

        g.setColour(waveformColour);
        for (int x = 0; x < width; ++x)
        {
            const int peakIndex = juce::jlimit(0,
                                               peakCount - 1,
                                               static_cast<int>((static_cast<float>(x) / widthScale) * static_cast<float>(peakCount - 1)));
            const float amplitude = juce::jlimit(0.0f, 1.0f, (*peaks)[static_cast<size_t>(peakIndex)]);
            const float drawHalf = juce::jmax(0.75f, amplitude * halfHeight);
            const int drawX = content.getX() + x;

            g.drawVerticalLine(drawX, centreY - drawHalf, centreY + drawHalf);
        }
    }

    void ResultsPanel::selectFirstRowIfNoneSelected()
    {
        if (!results.empty() && resultsList.getSelectedRow() < 0)
            resultsList.selectRow(0);
    }

    bool ResultsPanel::selectFile(int64_t rootId, const std::string &relativePath)
    {
        for (int i = 0; i < static_cast<int>(results.size()); ++i)
        {
            const auto &item = results[static_cast<size_t>(i)];
            if (item.rootId == rootId && item.relativePath == relativePath)
            {
                resultsList.selectRow(i);
                return true;
            }
        }

        return false;
    }

    bool ResultsPanel::selectRow(int row)
    {
        if (row < 0 || row >= static_cast<int>(results.size()))
            return false;

        resultsList.selectRow(row);
        return true;
    }

    int ResultsPanel::getSelectedRow() const noexcept
    {
        return resultsList.getSelectedRow();
    }

    int ResultsPanel::getResultCount() const noexcept
    {
        return static_cast<int>(results.size());
    }

    const FileRecord *ResultsPanel::getResultAt(int row) const noexcept
    {
        if (row < 0 || row >= static_cast<int>(results.size()))
            return nullptr;

        return &results[static_cast<size_t>(row)];
    }

    void ResultsPanel::setDarkMode(bool enabled)
    {
        if (darkModeEnabled == enabled)
            return;

        darkModeEnabled = enabled;

        const auto textColour = darkModeEnabled ? juce::Colours::white : juce::Colour(0xff202020);
        const auto editorBg = darkModeEnabled ? juce::Colour(0xff2b2b2b) : juce::Colour(0xffffffff);
        const auto outline = darkModeEnabled ? juce::Colour(0xff4d4d4d) : juce::Colour(0xffb8b8b8);
        const auto placeholder = darkModeEnabled ? juce::Colours::grey : juce::Colour(0xff7a7a7a);
        const auto comboBg = darkModeEnabled ? juce::Colour(0xff2b2b2b) : juce::Colour(0xffffffff);

        searchBox.setColour(juce::TextEditor::textColourId, textColour);
        searchBox.setColour(juce::TextEditor::backgroundColourId, editorBg);
        searchBox.setColour(juce::TextEditor::outlineColourId, outline);
        searchBox.setColour(juce::TextEditor::focusedOutlineColourId, darkModeEnabled ? juce::Colour(0xff6b9bc8) : juce::Colour(0xff2f6fa8));
        searchBox.setTextToShowWhenEmpty("Search samples...  (Ctrl+F)", placeholder);

        viewSelector.setColour(juce::ComboBox::textColourId, textColour);
        viewSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        viewSelector.setColour(juce::ComboBox::outlineColourId, outline);
        viewSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        sortSelector.setColour(juce::ComboBox::textColourId, textColour);
        sortSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        sortSelector.setColour(juce::ComboBox::outlineColourId, outline);
        sortSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        formatSelector.setColour(juce::ComboBox::textColourId, textColour);
        formatSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        formatSelector.setColour(juce::ComboBox::outlineColourId, outline);
        formatSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        channelsSelector.setColour(juce::ComboBox::textColourId, textColour);
        channelsSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        channelsSelector.setColour(juce::ComboBox::outlineColourId, outline);
        channelsSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        loopSelector.setColour(juce::ComboBox::textColourId, textColour);
        loopSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        loopSelector.setColour(juce::ComboBox::outlineColourId, outline);
        loopSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        for (auto *combo : {&bpmSelector, &keySelector})
        {
            combo->setColour(juce::ComboBox::textColourId, textColour);
            combo->setColour(juce::ComboBox::backgroundColourId, comboBg);
            combo->setColour(juce::ComboBox::outlineColourId, outline);
            combo->setColour(juce::ComboBox::arrowColourId, textColour);
        }

        favoriteToggle.setColour(juce::ToggleButton::textColourId, textColour);
        nearDuplicateBannerLabel.setColour(juce::Label::textColourId, darkModeEnabled ? juce::Colour(0xfff1d98a) : juce::Colour(0xff6b4e00));
        emptyStateLabel.setColour(juce::Label::textColourId, darkModeEnabled ? juce::Colour(0xffa8b3bf) : juce::Colour(0xff5a6570));

        resultsList.setColour(juce::ListBox::backgroundColourId, darkModeEnabled ? juce::Colour(0xff1e1e1e) : juce::Colour(0xfffafafa));

        updateFilterControlState();
        repaint();
    }

    void ResultsPanel::updateMetadataControlState()
    {
        const bool hasSelectedFile = getSelectedRow() >= 0;
        favoriteToggle.setEnabled(hasSelectedFile);
        nearDuplicatesButton.setEnabled(hasSelectedFile);
        revealButton.setEnabled(hasSelectedFile);
    }

    int ResultsPanel::getNumRows()
    {
        return static_cast<int>(results.size());
    }

    juce::var ResultsPanel::getDragSourceDescription(const juce::SparseSet<int> &rowsToDescribe)
    {
        if (rowsToDescribe.isEmpty() || onResolveAbsolutePathForFile == nullptr)
            return {};

        const int row = rowsToDescribe[0];
        const auto *file = getResultAt(row);
        if (file == nullptr)
            return {};

        const auto absolutePath = onResolveAbsolutePathForFile(*file);
        if (!absolutePath.has_value() || absolutePath->isEmpty())
            return {};

        const juce::File sourceFile(*absolutePath);
        if (!sourceFile.existsAsFile())
            return {};

        return sourceFile.getFullPathName();
    }

    bool ResultsPanel::shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails &sourceDetails,
                                                            juce::StringArray &files,
                                                            bool &canMoveFiles)
    {
        const auto filePath = sourceDetails.description.toString();
        if (filePath.isEmpty())
            return false;

        const juce::File file(filePath);
        if (!file.existsAsFile())
            return false;

        files.add(file.getFullPathName());
        canMoveFiles = false;
        return true;
    }

    void ResultsPanel::paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected)
    {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(results.size()))
            return;

        if (rowIsSelected)
            g.fillAll(darkModeEnabled ? juce::Colour(0xff2a3d55) : juce::Colour(0xffd8e8f8));

        const auto &item = results[static_cast<size_t>(rowNumber)];

        auto rowArea = juce::Rectangle<int>(0, 0, width, height).reduced(8, 4);
        auto waveformArea = rowArea.removeFromLeft(kWaveformColumnWidth);
        rowArea.removeFromLeft(8);

        paintWaveformPreview(g, waveformArea, item);

        auto titleRow = rowArea.removeFromTop(18);
        auto metadataRow = rowArea.removeFromTop(18);
        auto filenameArea = titleRow.removeFromLeft(titleRow.getWidth() / 2);
        auto pathArea = titleRow;

        g.setColour(darkModeEnabled ? juce::Colours::white : juce::Colour(0xff202020));
        g.setFont(13.0f);
        g.drawText(juce::String(item.filename), filenameArea, juce::Justification::centredLeft);

        g.setColour(darkModeEnabled ? juce::Colours::lightgrey : juce::Colour(0xff4a4a4a));
        const juce::String rightText = juce::String(item.relativePath) + (item.indexOnly ? "  [index-only]" : "");
        g.drawText(rightText, pathArea, juce::Justification::centredRight);

        if (isAcidizedLoop(item) || isAppleLoop(item))
        {
            const juce::String badgeText = isAcidizedLoop(item) ? "Acidized" : "Apple Loop";
            const int badgeWidth = isAcidizedLoop(item) ? 64 : 76;
            const int badgeHeight = 14;
            const int badgeX = juce::jmax(filenameArea.getX(), filenameArea.getRight() - badgeWidth - 4);
            const int badgeY = titleRow.getY() + 2;

            const auto badgeColour = isAcidizedLoop(item)
                                         ? (darkModeEnabled ? juce::Colour(0xff2f8f5b) : juce::Colour(0xff2f9f61))
                                         : (darkModeEnabled ? juce::Colour(0xff3465a4) : juce::Colour(0xff3a78bf));
            g.setColour(badgeColour);
            g.fillRoundedRectangle(static_cast<float>(badgeX), static_cast<float>(badgeY),
                                   static_cast<float>(badgeWidth), static_cast<float>(badgeHeight), 4.0f);

            g.setColour(juce::Colours::white);
            g.setFont(10.0f);
            g.drawText(badgeText, badgeX, badgeY, badgeWidth, badgeHeight, juce::Justification::centred);
        }

        g.setFont(11.0f);
        const auto labelColour = darkModeEnabled ? juce::Colours::silver : juce::Colour(0xff6a6a6a);
        const auto valueColour = darkModeEnabled ? juce::Colour(0xffcce9ff) : juce::Colour(0xff1d5c8d);
        drawMetadataTokens(g, metadataRow, buildMetadataTokens(item), labelColour, valueColour);
    }

    void ResultsPanel::selectedRowsChanged(int lastRowSelected)
    {
        if (suppressSelectionCallbacks)
            return;

        const auto *selectedFile = getResultAt(lastRowSelected);
        if (selectedFile == nullptr)
        {
            setSelectedFileMetadata(std::nullopt);
            return;
        }

        updateMetadataControlState();

        if (onFileSelected)
            onFileSelected(*selectedFile);
    }

    void ResultsPanel::listBoxItemDoubleClicked(int row, const juce::MouseEvent &)
    {
        const auto *selectedFile = getResultAt(row);
        if (selectedFile == nullptr)
            return;

        if (onFileActivated)
            onFileActivated(*selectedFile);
    }

    void ResultsPanel::returnKeyPressed(int lastRowSelected)
    {
        if (const auto *selectedFile = getResultAt(lastRowSelected); selectedFile != nullptr && onFileActivated)
            onFileActivated(*selectedFile);
    }

    juce::String ResultsPanel::getTooltipForRow(int row)
    {
        const auto *item = getResultAt(row);
        if (item == nullptr)
            return {};

        return juce::String(item->relativePath) + (item->indexOnly ? "  [index-only]" : "") + "\n" +
               metadataTokensToText(buildMetadataTokens(*item));
    }

} // namespace sw
