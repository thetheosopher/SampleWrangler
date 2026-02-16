#include "MainComponent.h"
#include "Util/Paths.h"

#include <JuceHeader.h>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <cmath>
#include <limits>
#include <exception>
#include <algorithm>

namespace sw
{

    namespace
    {
        constexpr int kToolbarHeight = 36;
        constexpr int kSplitterThickness = 5;
        constexpr int kMinLeftPanelWidth = 180;
        constexpr int kMinRightPanelWidth = 320;
        constexpr int kMinResultsHeight = 120;
        constexpr int kMinBottomHeight = 120;
        constexpr int kMinPreviewWidth = 220;
        constexpr int kMinWaveformWidth = 220;
        constexpr float kDefaultLeftPanelRatio = 0.24f;
        constexpr float kDefaultBottomPanelRatio = 0.24f;
        constexpr float kDefaultPreviewPanelRatio = 0.35f;
    }

    namespace
    {
        std::unique_ptr<juce::Drawable> createFolderIcon(const juce::Colour colour)
        {
            auto drawable = std::make_unique<juce::DrawablePath>();
            juce::Path icon;
            icon.addRoundedRectangle(2.0f, 8.0f, 20.0f, 12.0f, 2.0f);
            icon.addRoundedRectangle(4.0f, 4.0f, 9.0f, 5.0f, 1.5f);
            drawable->setPath(icon);
            drawable->setFill(colour);
            return drawable;
        }

        std::unique_ptr<juce::Drawable> createRescanIcon(const juce::Colour colour)
        {
            auto drawable = std::make_unique<juce::DrawablePath>();
            juce::Path icon;
            icon.addCentredArc(12.0f, 12.0f, 8.0f, 8.0f, 0.0f,
                               juce::MathConstants<float>::pi * 0.25f,
                               juce::MathConstants<float>::pi * 1.75f,
                               true);
            icon.addTriangle(20.0f, 9.0f, 23.0f, 12.0f, 19.0f, 14.0f);
            drawable->setPath(icon);
            drawable->setStrokeFill(colour);
            drawable->setStrokeType(juce::PathStrokeType(2.2f));
            return drawable;
        }

        std::unique_ptr<juce::Drawable> createCancelIcon(const juce::Colour colour)
        {
            auto drawable = std::make_unique<juce::DrawablePath>();
            juce::Path icon;
            icon.addEllipse(3.0f, 3.0f, 18.0f, 18.0f);
            icon.startNewSubPath(8.0f, 8.0f);
            icon.lineTo(16.0f, 16.0f);
            icon.startNewSubPath(16.0f, 8.0f);
            icon.lineTo(8.0f, 16.0f);
            drawable->setPath(icon);
            drawable->setStrokeFill(colour);
            drawable->setStrokeType(juce::PathStrokeType(2.0f));
            return drawable;
        }

        std::unique_ptr<juce::Drawable> createResetLayoutIcon(const juce::Colour colour)
        {
            auto drawable = std::make_unique<juce::DrawablePath>();
            juce::Path icon;
            icon.addRectangle(3.0f, 3.0f, 8.0f, 8.0f);
            icon.addRectangle(13.0f, 3.0f, 8.0f, 8.0f);
            icon.addRectangle(3.0f, 13.0f, 8.0f, 8.0f);
            icon.addRectangle(13.0f, 13.0f, 8.0f, 8.0f);
            drawable->setPath(icon);
            drawable->setStrokeFill(colour);
            drawable->setStrokeType(juce::PathStrokeType(1.8f));
            return drawable;
        }
    }

    MainComponent::MainComponent()
    {
        addAndMakeVisible(toolbar);
        addAndMakeVisible(addRootToolbarButton);
        addAndMakeVisible(rescanToolbarButton);
        addAndMakeVisible(cancelScanToolbarButton);
        addAndMakeVisible(resetLayoutToolbarButton);
        addAndMakeVisible(browserPanel);
        addAndMakeVisible(leftRightSplitter);
        addAndMakeVisible(resultsPanel);
        addAndMakeVisible(resultsBottomSplitter);
        addAndMakeVisible(waveformPanel);
        addAndMakeVisible(previewPanel);
        addAndMakeVisible(previewWaveformSplitter);

        leftRightSplitter.onDragged = [this](int deltaPixels)
        {
            auto content = getLocalBounds();
            content.removeFromTop(kToolbarHeight);

            const int totalWidth = content.getWidth() - kSplitterThickness;
            if (totalWidth <= (kMinLeftPanelWidth + kMinRightPanelWidth))
                return;

            int currentLeftWidth = static_cast<int>(leftPanelRatio * static_cast<float>(totalWidth));
            int nextLeftWidth = currentLeftWidth + deltaPixels;
            nextLeftWidth = juce::jlimit(kMinLeftPanelWidth, totalWidth - kMinRightPanelWidth, nextLeftWidth);

            leftPanelRatio = static_cast<float>(nextLeftWidth) / static_cast<float>(totalWidth);
            resized();
        };
        leftRightSplitter.onDragEnded = [this]
        {
            persistLayoutSettings();
        };

        resultsBottomSplitter.onDragged = [this](int deltaPixels)
        {
            auto content = getLocalBounds();
            content.removeFromTop(kToolbarHeight);

            const int totalWidth = content.getWidth() - kSplitterThickness;
            if (totalWidth <= (kMinLeftPanelWidth + kMinRightPanelWidth))
                return;

            int leftWidth = static_cast<int>(leftPanelRatio * static_cast<float>(totalWidth));
            leftWidth = juce::jlimit(kMinLeftPanelWidth, totalWidth - kMinRightPanelWidth, leftWidth);

            juce::Rectangle<int> rightArea = content;
            rightArea.removeFromLeft(leftWidth + kSplitterThickness);

            const int totalHeight = rightArea.getHeight() - kSplitterThickness;
            if (totalHeight <= (kMinResultsHeight + kMinBottomHeight))
                return;

            int currentBottomHeight = static_cast<int>(bottomPanelRatio * static_cast<float>(totalHeight));
            int nextBottomHeight = currentBottomHeight - deltaPixels;
            nextBottomHeight = juce::jlimit(kMinBottomHeight, totalHeight - kMinResultsHeight, nextBottomHeight);

            bottomPanelRatio = static_cast<float>(nextBottomHeight) / static_cast<float>(totalHeight);
            resized();
        };
        resultsBottomSplitter.onDragEnded = [this]
        {
            persistLayoutSettings();
        };

        previewWaveformSplitter.onDragged = [this](int deltaPixels)
        {
            auto content = getLocalBounds();
            content.removeFromTop(kToolbarHeight);

            const int totalWidth = content.getWidth() - kSplitterThickness;
            if (totalWidth <= (kMinLeftPanelWidth + kMinRightPanelWidth))
                return;

            int leftWidth = static_cast<int>(leftPanelRatio * static_cast<float>(totalWidth));
            leftWidth = juce::jlimit(kMinLeftPanelWidth, totalWidth - kMinRightPanelWidth, leftWidth);

            juce::Rectangle<int> rightArea = content;
            rightArea.removeFromLeft(leftWidth + kSplitterThickness);

            const int totalHeight = rightArea.getHeight() - kSplitterThickness;
            if (totalHeight <= (kMinResultsHeight + kMinBottomHeight))
                return;

            int bottomHeight = static_cast<int>(bottomPanelRatio * static_cast<float>(totalHeight));
            bottomHeight = juce::jlimit(kMinBottomHeight, totalHeight - kMinResultsHeight, bottomHeight);

            juce::Rectangle<int> bottomArea = rightArea;
            bottomArea.removeFromTop(rightArea.getHeight() - bottomHeight);

            const int splitWidth = bottomArea.getWidth() - kSplitterThickness;
            if (splitWidth <= (kMinPreviewWidth + kMinWaveformWidth))
                return;

            int currentPreviewWidth = static_cast<int>(previewPanelRatio * static_cast<float>(splitWidth));
            int nextPreviewWidth = currentPreviewWidth + deltaPixels;
            nextPreviewWidth = juce::jlimit(kMinPreviewWidth, splitWidth - kMinWaveformWidth, nextPreviewWidth);

            previewPanelRatio = static_cast<float>(nextPreviewWidth) / static_cast<float>(splitWidth);
            resized();
        };
        previewWaveformSplitter.onDragEnded = [this]
        {
            persistLayoutSettings();
        };

        browserPanel.onRootSelected = [this](std::optional<int64_t> rootId)
        {
            selectedRootFilterId = rootId;
            refreshResults(currentSearchQuery);
        };

        addRootToolbarButton.setImages(createFolderIcon(juce::Colours::white).release(),
                                       createFolderIcon(juce::Colours::lightgrey).release());
        addRootToolbarButton.setTooltip("Add a root folder to the library");
        addRootToolbarButton.onClick = [this]
        {
            handleAddRootClicked();
        };

        rescanToolbarButton.setImages(createRescanIcon(juce::Colours::white).release(),
                                      createRescanIcon(juce::Colours::lightgrey).release());
        rescanToolbarButton.setTooltip("Rescan all configured roots");
        rescanToolbarButton.onClick = [this]
        {
            handleRescanAllClicked();
        };

        cancelScanToolbarButton.setImages(createCancelIcon(juce::Colours::white).release(),
                                          createCancelIcon(juce::Colours::lightgrey).release());
        cancelScanToolbarButton.setTooltip("Cancel the active scan");
        cancelScanToolbarButton.onClick = [this]
        {
            cancelScan();
        };

        resetLayoutToolbarButton.setImages(createResetLayoutIcon(juce::Colours::white).release(),
                                           createResetLayoutIcon(juce::Colours::lightgrey).release());
        resetLayoutToolbarButton.setTooltip("Reset splitter layout to defaults");
        resetLayoutToolbarButton.onClick = [this]
        {
            resetLayout();
        };

        updateToolbarScanState(false);

        const auto appDataDir = defaultCacheDirectory();
        std::filesystem::create_directories(std::filesystem::path(appDataDir).parent_path());

        const auto dbPath = defaultDatabasePath();
        if (!catalogDb.open(dbPath))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Database Error",
                                                   "Failed to open catalog database:\n" + juce::String(dbPath));
        }
        else
        {
            restoreLayoutSettings();
        }

        resultsPanel.onSearchQueryChanged = [this](const std::string &query)
        {
            refreshResults(query);
        };

        resultsPanel.onFileSelected = [this](const FileRecord &file)
        {
            handleFileSelected(file, false, false);
        };

        resultsPanel.onFileActivated = [this](const FileRecord &file)
        {
            handleFileSelected(file, true, true);
        };

        previewPanel.onPlayRequested = [this]
        {
            audioEngine.play();
            waveformPanel.setPlayheadNormalized(0.0f);
        };

        previewPanel.onStopRequested = [this]
        {
            audioEngine.stop();
            waveformPanel.setPlayheadNormalized(-1.0f);
        };

        previewPanel.onPitchChanged = [this](double semitones)
        {
            audioEngine.setPitchSemitones(semitones);
            persistPreviewPitch(semitones);
        };

        previewPanel.onApplyOutputDeviceTypeRequested = [this](const juce::String &typeName)
        {
            juce::String error;
            if (!audioEngine.setCurrentOutputDeviceType(typeName, &error))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Audio Device Type Error",
                                                       "Failed to switch output device type:\n" + error);
            }
            else
            {
                persistAudioDeviceType(typeName);
            }

            refreshOutputDeviceTypeList();
            refreshOutputDeviceList();
        };

        previewPanel.onApplyOutputDeviceRequested = [this](const juce::String &deviceName)
        {
            juce::String error;
            if (!audioEngine.setCurrentOutputDevice(deviceName, &error))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Audio Device Error",
                                                       "Failed to switch output device:\n" + error);
            }
            else
            {
                persistAudioDeviceName(deviceName);
            }
            refreshOutputDeviceList();
        };

        previewPanel.onMidiInputDeviceSelected = [this](const juce::String &deviceIdentifier)
        {
            applyMidiInputSelection(deviceIdentifier, true);
        };

        audioEngine.initialiseDeviceManager();
        restoreAudioDeviceSettings();
        restorePreviewSettings();
        restoreMidiInputSettings();
        refreshOutputDeviceTypeList();
        refreshOutputDeviceList();
        refreshMidiInputDeviceList(true);

        midiRouter.setMidiCallback([this](const juce::MidiMessage &message)
                                   { audioEngine.handleMidiMessage(message); });
        midiRouter.attachKeyboardState(previewPanel.getKeyboardState());

        refreshMidiInputDeviceList(true);

        refreshRoots();
        refreshResults();
        restoreLastSelection();
        restoreScanSummaryStatus();
        browserPanel.setScanInProgress(false);
        updateToolbarScanState(false);
        startTimerHz(30);

        setSize(1200, 800);
    }

    MainComponent::~MainComponent() = default;

    void MainComponent::paint(juce::Graphics &g)
    {
        auto toolbarBounds = getLocalBounds().removeFromTop(kToolbarHeight);
        g.setColour(juce::Colour(0xff272c33));
        g.fillRect(toolbarBounds);

        if (toolbarFeedbackTicksRemaining > 0 && toolbarFeedbackText.isNotEmpty())
        {
            g.setColour(juce::Colours::lightgreen);
            g.setFont(12.0f);
            g.drawFittedText(toolbarFeedbackText,
                             toolbarBounds.reduced(10, 0),
                             juce::Justification::centredRight,
                             1);
        }

        g.setColour(juce::Colours::darkgrey.withAlpha(0.9f));
        g.drawHorizontalLine(toolbarBounds.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
    }

    MainComponent::SplitterBar::SplitterBar(Orientation orientationIn)
        : orientation(orientationIn)
    {
        setMouseCursor(orientation == Orientation::vertical
                           ? juce::MouseCursor::LeftRightResizeCursor
                           : juce::MouseCursor::UpDownResizeCursor);
    }

    void MainComponent::SplitterBar::paint(juce::Graphics &g)
    {
        g.fillAll(juce::Colours::darkgrey.withAlpha(0.75f));
    }

    void MainComponent::SplitterBar::mouseDown(const juce::MouseEvent &event)
    {
        lastScreenPosition = event.getScreenPosition();
    }

    void MainComponent::SplitterBar::mouseDrag(const juce::MouseEvent &event)
    {
        const auto screenPosition = event.getScreenPosition();
        const int delta = orientation == Orientation::vertical
                              ? (screenPosition.x - lastScreenPosition.x)
                              : (screenPosition.y - lastScreenPosition.y);

        lastScreenPosition = screenPosition;
        if (onDragged != nullptr)
            onDragged(delta);
    }

    void MainComponent::SplitterBar::mouseUp(const juce::MouseEvent &)
    {
        if (onDragEnded != nullptr)
            onDragEnded();
    }

    void MainComponent::refreshRoots()
    {
        const auto roots = catalogDb.allRoots();

        if (selectedRootFilterId.has_value())
        {
            const auto it = std::find_if(roots.begin(), roots.end(), [this](const RootRecord &root)
                                         { return root.id == *selectedRootFilterId; });

            if (it == roots.end())
                selectedRootFilterId.reset();
        }

        browserPanel.setRoots(roots);
        browserPanel.setSelectedRootId(selectedRootFilterId);
    }

    void MainComponent::refreshResults(const std::string &query)
    {
        currentSearchQuery = query;

        if (!selectedRootFilterId.has_value())
        {
            if (query.empty())
                resultsPanel.setResults(catalogDb.listRecentFiles(300));
            else
                resultsPanel.setResults(catalogDb.searchFiles(query + "*", 300));
            return;
        }

        const int64_t rootId = *selectedRootFilterId;
        if (query.empty())
            resultsPanel.setResults(catalogDb.listRecentFilesByRoot(rootId, 300));
        else
            resultsPanel.setResults(catalogDb.searchFilesByRoot(rootId, query + "*", 300));
    }

    void MainComponent::refreshOutputDeviceList()
    {
        previewPanel.setAvailableOutputDevices(audioEngine.getAvailableOutputDevices(),
                                               audioEngine.getCurrentOutputDeviceName());
    }

    void MainComponent::refreshOutputDeviceTypeList()
    {
        previewPanel.setAvailableOutputDeviceTypes(audioEngine.getAvailableOutputDeviceTypes(),
                                                   audioEngine.getCurrentOutputDeviceType());
    }

    void MainComponent::refreshMidiInputDeviceList(bool forceRefresh)
    {
        const auto devices = juce::MidiInput::getAvailableDevices();
        juce::StringArray identifiers;
        identifiers.ensureStorageAllocated(devices.size());
        for (const auto &device : devices)
            identifiers.add(device.identifier);

        const bool changed = forceRefresh || identifiers != lastKnownMidiInputIdentifiers;
        if (!changed)
            return;

        lastKnownMidiInputIdentifiers = identifiers;

        bool selectedDeviceStillAvailable = false;
        for (const auto &device : devices)
        {
            if (device.identifier == selectedMidiInputIdentifier)
            {
                selectedDeviceStillAvailable = true;
                break;
            }
        }

        if (!selectedDeviceStillAvailable)
        {
            if (!devices.isEmpty())
                selectedMidiInputIdentifier = devices.getFirst().identifier;
            else
                selectedMidiInputIdentifier.clear();

            applyMidiInputSelection(selectedMidiInputIdentifier, true);
        }

        previewPanel.setAvailableMidiInputDevices(devices, selectedMidiInputIdentifier);
    }

    void MainComponent::applyMidiInputSelection(const juce::String &deviceIdentifier, bool persistSelection)
    {
        selectedMidiInputIdentifier = deviceIdentifier;

        midiRouter.disableAllDevices();
        if (selectedMidiInputIdentifier.isNotEmpty())
            midiRouter.enableDevice(selectedMidiInputIdentifier);

        if (persistSelection)
            persistMidiInputSelection(selectedMidiInputIdentifier);

        previewPanel.setAvailableMidiInputDevices(juce::MidiInput::getAvailableDevices(), selectedMidiInputIdentifier);
    }

    void MainComponent::restoreAudioDeviceSettings()
    {
        if (const auto savedType = catalogDb.getAppSetting("audio.outputDeviceType"))
        {
            juce::String ignored;
            audioEngine.setCurrentOutputDeviceType(*savedType, &ignored);
        }

        if (const auto savedDevice = catalogDb.getAppSetting("audio.outputDeviceName"))
        {
            juce::String ignored;
            audioEngine.setCurrentOutputDevice(*savedDevice, &ignored);
        }
    }

    void MainComponent::persistAudioDeviceType(const juce::String &typeName)
    {
        catalogDb.setAppSetting("audio.outputDeviceType", typeName.toStdString());
    }

    void MainComponent::persistAudioDeviceName(const juce::String &deviceName)
    {
        catalogDb.setAppSetting("audio.outputDeviceName", deviceName.toStdString());
    }

    void MainComponent::persistMidiInputSelection(const juce::String &deviceIdentifier)
    {
        catalogDb.setAppSetting("midi.inputDeviceIdentifier", deviceIdentifier.toStdString());
    }

    void MainComponent::restorePreviewSettings()
    {
        const auto savedPitch = catalogDb.getAppSetting("preview.pitchSemitones");
        if (!savedPitch.has_value())
            return;

        try
        {
            const double parsed = std::stod(*savedPitch);
            const double clamped = juce::jlimit(-24.0, 24.0, parsed);
            previewPanel.setPitchSemitones(clamped);
            audioEngine.setPitchSemitones(clamped);
        }
        catch (const std::exception &)
        {
        }
    }

    void MainComponent::restoreMidiInputSettings()
    {
        if (const auto saved = catalogDb.getAppSetting("midi.inputDeviceIdentifier"))
            selectedMidiInputIdentifier = *saved;
    }

    void MainComponent::persistPreviewPitch(double semitones)
    {
        catalogDb.setAppSetting("preview.pitchSemitones", juce::String(semitones).toStdString());
    }

    void MainComponent::restoreLayoutSettings()
    {
        if (const auto value = catalogDb.getAppSetting("layout.leftPanelRatio"))
        {
            try
            {
                leftPanelRatio = juce::jlimit(0.15f, 0.55f, std::stof(*value));
            }
            catch (const std::exception &)
            {
            }
        }

        if (const auto value = catalogDb.getAppSetting("layout.bottomPanelRatio"))
        {
            try
            {
                bottomPanelRatio = juce::jlimit(0.15f, 0.55f, std::stof(*value));
            }
            catch (const std::exception &)
            {
            }
        }

        if (const auto value = catalogDb.getAppSetting("layout.previewPanelRatio"))
        {
            try
            {
                previewPanelRatio = juce::jlimit(0.20f, 0.70f, std::stof(*value));
            }
            catch (const std::exception &)
            {
            }
        }
    }

    void MainComponent::persistLayoutSettings()
    {
        catalogDb.setAppSetting("layout.leftPanelRatio", juce::String(leftPanelRatio, 4).toStdString());
        catalogDb.setAppSetting("layout.bottomPanelRatio", juce::String(bottomPanelRatio, 4).toStdString());
        catalogDb.setAppSetting("layout.previewPanelRatio", juce::String(previewPanelRatio, 4).toStdString());
    }

    void MainComponent::restoreLastSelection()
    {
        const auto savedRootId = catalogDb.getAppSetting("preview.lastSelectedRootId");
        const auto savedRelPath = catalogDb.getAppSetting("preview.lastSelectedRelativePath");

        if (!savedRootId.has_value() || !savedRelPath.has_value())
            return;

        try
        {
            const auto rootId = static_cast<int64_t>(std::stoll(*savedRootId));

            if (resultsPanel.selectFile(rootId, *savedRelPath))
                return;

            if (const auto file = catalogDb.fileByRootAndRelativePath(rootId, *savedRelPath))
                handleFileSelected(*file, false, false);
        }
        catch (const std::exception &)
        {
        }
    }

    void MainComponent::persistLastSelectedFile(const FileRecord &file)
    {
        catalogDb.setAppSetting("preview.lastSelectedRootId", juce::String(file.rootId).toStdString());
        catalogDb.setAppSetting("preview.lastSelectedRelativePath", file.relativePath);
    }

    void MainComponent::restoreScanSummaryStatus()
    {
        if (const auto status = catalogDb.getAppSetting("scan.lastSummaryStatus"))
        {
            browserPanel.setScanStatus(*status);
            return;
        }

        browserPanel.setScanStatus("Idle");
    }

    void MainComponent::persistScanSummaryStatus(const juce::String &statusText)
    {
        catalogDb.setAppSetting("scan.lastSummaryStatus", statusText.toStdString());
    }

    std::string MainComponent::rootPathForId(int64_t rootId)
    {
        const auto roots = catalogDb.allRoots();
        const auto it = std::find_if(roots.begin(), roots.end(), [rootId](const RootRecord &root)
                                     { return root.id == rootId; });

        if (it == roots.end())
            return {};

        return it->path;
    }

    void MainComponent::handleFileSelected(const FileRecord &file, bool playWhenReady, bool showIndexOnlyAlert)
    {
        persistLastSelectedFile(file);

        if (file.indexOnly)
        {
            waveformPanel.setPeaks({});
            waveformPanel.setPlayheadNormalized(-1.0f);
            if (showIndexOnlyAlert)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                       "Index-only format",
                                                       "This format is indexed for search but not playable in MVP.");
            }
            return;
        }

        const auto rootPath = rootPathForId(file.rootId);
        if (rootPath.empty())
            return;

        const auto absolutePath = resolveAbsolutePath(rootPath, file.relativePath);
        juce::Component::SafePointer<MainComponent> safeThis(this);

        jobQueue.enqueue(Job{
            [safeThis, absolutePath, playWhenReady](const std::atomic<uint64_t> &cancelGeneration, uint64_t jobGeneration)
            {
                if (cancelGeneration.load(std::memory_order_relaxed) != jobGeneration)
                    return;

                juce::AudioFormatManager formatManager;
                formatManager.registerBasicFormats();

                juce::File sourceFile(absolutePath);
                auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(sourceFile));
                if (!reader)
                    return;

                const int numChannels = std::max(1, static_cast<int>(std::min<uint32_t>(2, reader->numChannels)));
                const int numSamples = static_cast<int>(std::min<int64_t>(reader->lengthInSamples, static_cast<int64_t>(std::numeric_limits<int>::max())));

                if (numSamples <= 0)
                    return;

                juce::AudioBuffer<float> tempBuffer(numChannels, numSamples);
                if (!reader->read(&tempBuffer, 0, numSamples, 0, true, true))
                    return;

                constexpr int targetPeakCount = 1600;
                const int peakCount = std::max(1, std::min(targetPeakCount, numSamples));
                const int samplesPerPeak = std::max(1, numSamples / peakCount);

                auto peaks = std::make_shared<std::vector<float>>();
                peaks->resize(static_cast<size_t>(peakCount), 0.0f);

                for (int p = 0; p < peakCount; ++p)
                {
                    const int start = p * samplesPerPeak;
                    const int end = std::min(numSamples, start + samplesPerPeak);

                    float maxAbs = 0.0f;
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        const auto *channelData = tempBuffer.getReadPointer(ch);
                        for (int s = start; s < end; ++s)
                            maxAbs = std::max(maxAbs, std::abs(channelData[s]));
                    }

                    (*peaks)[static_cast<size_t>(p)] = maxAbs;
                }

                auto interleaved = std::make_shared<std::vector<float>>();
                interleaved->resize(static_cast<size_t>(numChannels * numSamples));
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto *src = tempBuffer.getReadPointer(ch);
                    for (int s = 0; s < numSamples; ++s)
                        (*interleaved)[static_cast<size_t>(ch * numSamples + s)] = src[s];
                }

                const double sampleRate = reader->sampleRate;

                juce::MessageManager::callAsync([safeThis, interleaved, peaks, numChannels, numSamples, sampleRate, playWhenReady]
                                                {
                    if (safeThis == nullptr)
                        return;

                    auto previewBuffer = std::make_unique<juce::AudioBuffer<float>>(numChannels, numSamples);
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        auto *dst = previewBuffer->getWritePointer(ch);
                        for (int s = 0; s < numSamples; ++s)
                            dst[s] = (*interleaved)[static_cast<size_t>(ch * numSamples + s)];
                    }

                    safeThis->audioEngine.loadPreviewBuffer(std::move(previewBuffer), sampleRate);
                    safeThis->waveformPanel.setPeaks(*peaks);
                    safeThis->waveformPanel.setPlayheadNormalized(-1.0f);
                    if (playWhenReady)
                        safeThis->audioEngine.play(); });
            },
            JobPriority::High});
    }

    void MainComponent::timerCallback()
    {
        ++midiDeviceRefreshCounter;
        if (midiDeviceRefreshCounter >= 30)
        {
            midiDeviceRefreshCounter = 0;
            refreshMidiInputDeviceList(false);
        }

        if (toolbarFeedbackTicksRemaining > 0)
        {
            --toolbarFeedbackTicksRemaining;
            repaint(0, 0, getWidth(), kToolbarHeight);

            if (toolbarFeedbackTicksRemaining == 0)
                toolbarFeedbackText.clear();
        }

        if (!audioEngine.isPreviewPlaying())
        {
            waveformPanel.setPlayheadNormalized(-1.0f);
            return;
        }

        const float progress = static_cast<float>(audioEngine.getPreviewPlaybackProgressNormalized());
        waveformPanel.setPlayheadNormalized(progress);
    }

    void MainComponent::handleAddRootClicked()
    {
        juce::File initialDirectory;
        if (const auto savedRootPath = catalogDb.getAppSetting("roots.lastAddedPath"))
            initialDirectory = juce::File(*savedRootPath);

        rootChooser = std::make_unique<juce::FileChooser>("Select a root folder", initialDirectory);

        constexpr auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;

        rootChooser->launchAsync(chooserFlags, [this](const juce::FileChooser &chooser)
                                 {
            const auto selected = chooser.getResult();
            if (!selected.isDirectory())
                return;

            const auto path = selected.getFullPathName().toStdString();
            const auto label = selected.getFileName().toStdString();

            if (!catalogDb.addRoot(path, label))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Add Root Failed",
                                                       "Could not add root:\n" + selected.getFullPathName());
                return;
            }

            catalogDb.setAppSetting("roots.lastAddedPath", path);

            refreshRoots();
            refreshResults();

            const auto roots = catalogDb.allRoots();
            const auto it = std::find_if(roots.begin(), roots.end(), [&path](const RootRecord &root)
            {
                return root.path == path;
            });

            if (it == roots.end())
                return;

            startRootScan(it->id, path, juce::String(label)); });
    }

    void MainComponent::startRootScan(int64_t rootId,
                                      const std::string &rootPath,
                                      const juce::String &rootDisplayName,
                                      std::function<void()> onCompleted)
    {
        scanInProgress = true;
        updateToolbarScanState(true);
        scannedFilesCount = 0;
        scanStartTime = std::chrono::steady_clock::now();
        const juce::String rootProgressPrefix = rescanAllInProgress
                                                    ? ("Root " + juce::String(rescanCurrentRootIndex) + "/" + juce::String(rescanTotalRoots) + " - ")
                                                    : juce::String();
        browserPanel.setScanStatus("Scanning " + rootProgressPrefix + "[" + rootDisplayName + "]... 0 files (0.0s)");
        browserPanel.setScanInProgress(true);

        scanner.scanRoot(
            rootId,
            rootPath,
            [this, rootDisplayName](const std::string &)
            {
                juce::MessageManager::callAsync([this, rootDisplayName]
                                                {
                    ++scannedFilesCount;
                    const auto now = std::chrono::steady_clock::now();
                    const auto elapsedSec = std::chrono::duration<double>(now - scanStartTime).count();
                    const juce::String rootProgressPrefix = rescanAllInProgress
                                                          ? ("Root " + juce::String(rescanCurrentRootIndex) + "/" + juce::String(rescanTotalRoots) + " - ")
                                                          : juce::String();
                    browserPanel.setScanStatus("Scanning " + rootProgressPrefix + "[" + rootDisplayName + "]... " + juce::String(scannedFilesCount)
                                               + " files (" + juce::String(elapsedSec, 1) + "s)");

                    if ((scannedFilesCount % 25) == 0)
                        refreshResults(); });
            },
            [this, onCompleted, rootDisplayName]()
            {
                juce::MessageManager::callAsync([this, onCompleted, rootDisplayName]
                                                {
                    scanInProgress = false;
                    const auto now = std::chrono::steady_clock::now();
                    const auto elapsedSec = std::chrono::duration<double>(now - scanStartTime).count();
                    const juce::String rootProgressPrefix = rescanAllInProgress
                                                          ? ("Root " + juce::String(rescanCurrentRootIndex) + "/" + juce::String(rescanTotalRoots) + " - ")
                                                          : juce::String();
                    const auto summary = "Idle (last " + rootProgressPrefix + "[" + rootDisplayName + "]: " + juce::String(scannedFilesCount)
                                       + " files in " + juce::String(elapsedSec, 1) + "s)";
                    browserPanel.setScanStatus(summary);
                    persistScanSummaryStatus(summary);
                    browserPanel.setScanInProgress(false);
                    updateToolbarScanState(false);
                    refreshResults();
                    resultsPanel.selectFirstRowIfNoneSelected();

                    if (onCompleted)
                        onCompleted(); });
            });
    }

    void MainComponent::handleRescanAllClicked()
    {
        if (scanInProgress)
            return;

        const auto roots = catalogDb.allRoots();
        if (roots.empty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                   "Rescan All",
                                                   "No roots configured to scan.");
            return;
        }

        rescanAllInProgress = true;
        rescanCurrentRootIndex = 0;
        rescanTotalRoots = static_cast<int>(roots.size());
        auto sharedRoots = std::make_shared<std::vector<RootRecord>>(roots);
        auto runNext = std::make_shared<std::function<void(size_t)>>();

        *runNext = [this, sharedRoots, runNext](size_t index)
        {
            if (!rescanAllInProgress)
                return;

            if (index >= sharedRoots->size())
            {
                const int completedRoots = rescanTotalRoots;
                rescanAllInProgress = false;
                rescanCurrentRootIndex = 0;
                rescanTotalRoots = 0;

                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                       "Rescan All Complete",
                                                       "Scanned " + juce::String(completedRoots) + " roots.");
                return;
            }

            rescanCurrentRootIndex = static_cast<int>(index) + 1;
            const auto &root = (*sharedRoots)[index];
            startRootScan(root.id, root.path, juce::String(root.label), [this, sharedRoots, runNext, index]()
                          {
                if (!rescanAllInProgress)
                    return;
                (*runNext)(index + 1); });
        };

        (*runNext)(0);
    }

    void MainComponent::cancelScan()
    {
        if (!scanInProgress)
            return;

        rescanAllInProgress = false;
        rescanCurrentRootIndex = 0;
        rescanTotalRoots = 0;

        const auto now = std::chrono::steady_clock::now();
        const auto elapsedSec = std::chrono::duration<double>(now - scanStartTime).count();
        browserPanel.setScanStatus("Cancelling... (" + juce::String(elapsedSec, 1) + "s)");

        const int cancelledCount = scannedFilesCount;
        const double cancelledElapsedSec = elapsedSec;
        jobQueue.cancelAll();

        juce::MessageManager::callAsync([this, cancelledCount, cancelledElapsedSec]
                                        {
            scanInProgress = false;
            const auto summary = "Idle (cancelled: " + juce::String(cancelledCount)
                               + " files in " + juce::String(cancelledElapsedSec, 1) + "s)";
            browserPanel.setScanStatus(summary);
            persistScanSummaryStatus(summary);
            browserPanel.setScanInProgress(false);
            updateToolbarScanState(false);
            refreshResults();
            resultsPanel.selectFirstRowIfNoneSelected(); });
    }

    void MainComponent::updateToolbarScanState(bool inProgress)
    {
        addRootToolbarButton.setEnabled(!inProgress);
        rescanToolbarButton.setEnabled(!inProgress);
        cancelScanToolbarButton.setEnabled(inProgress);
    }

    void MainComponent::resetLayout()
    {
        leftPanelRatio = kDefaultLeftPanelRatio;
        bottomPanelRatio = kDefaultBottomPanelRatio;
        previewPanelRatio = kDefaultPreviewPanelRatio;
        resized();
        persistLayoutSettings();
        toolbarFeedbackText = "Layout reset";
        toolbarFeedbackTicksRemaining = 60;
        repaint(0, 0, getWidth(), kToolbarHeight);
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();

        auto toolbarArea = area.removeFromTop(kToolbarHeight).reduced(6, 4);
        toolbar.setBounds(toolbarArea);

        constexpr int iconButtonSize = 28;
        constexpr int toolbarGap = 6;
        addRootToolbarButton.setBounds(toolbarArea.removeFromLeft(iconButtonSize));
        toolbarArea.removeFromLeft(toolbarGap);
        rescanToolbarButton.setBounds(toolbarArea.removeFromLeft(iconButtonSize));
        toolbarArea.removeFromLeft(toolbarGap);
        cancelScanToolbarButton.setBounds(toolbarArea.removeFromLeft(iconButtonSize));
        toolbarArea.removeFromLeft(toolbarGap);
        resetLayoutToolbarButton.setBounds(toolbarArea.removeFromLeft(iconButtonSize));

        const int totalWidth = area.getWidth() - kSplitterThickness;
        int leftWidth = static_cast<int>(leftPanelRatio * static_cast<float>(totalWidth));
        leftWidth = juce::jlimit(kMinLeftPanelWidth, juce::jmax(kMinLeftPanelWidth, totalWidth - kMinRightPanelWidth), leftWidth);

        browserPanel.setBounds(area.removeFromLeft(leftWidth));
        leftRightSplitter.setBounds(area.removeFromLeft(kSplitterThickness));

        auto rightArea = area;
        const int totalHeight = rightArea.getHeight() - kSplitterThickness;
        int bottomHeight = static_cast<int>(bottomPanelRatio * static_cast<float>(totalHeight));
        bottomHeight = juce::jlimit(kMinBottomHeight, juce::jmax(kMinBottomHeight, totalHeight - kMinResultsHeight), bottomHeight);

        resultsPanel.setBounds(rightArea.removeFromTop(rightArea.getHeight() - bottomHeight));
        resultsBottomSplitter.setBounds(rightArea.removeFromTop(kSplitterThickness));

        auto bottomArea = rightArea;
        const int bottomWidth = bottomArea.getWidth() - kSplitterThickness;
        int previewWidth = static_cast<int>(previewPanelRatio * static_cast<float>(bottomWidth));
        previewWidth = juce::jlimit(kMinPreviewWidth, juce::jmax(kMinPreviewWidth, bottomWidth - kMinWaveformWidth), previewWidth);

        previewPanel.setBounds(bottomArea.removeFromLeft(previewWidth));
        previewWaveformSplitter.setBounds(bottomArea.removeFromLeft(kSplitterThickness));
        waveformPanel.setBounds(bottomArea);
    }

} // namespace sw
