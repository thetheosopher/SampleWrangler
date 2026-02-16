#include "MainComponent.h"
#include "Util/Paths.h"

#include <JuceHeader.h>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <cmath>
#include <limits>
#include <exception>

namespace sw
{

    MainComponent::MainComponent()
    {
        addAndMakeVisible(browserPanel);
        addAndMakeVisible(resultsPanel);
        addAndMakeVisible(waveformPanel);
        addAndMakeVisible(previewPanel);

        const auto appDataDir = defaultCacheDirectory();
        std::filesystem::create_directories(std::filesystem::path(appDataDir).parent_path());

        const auto dbPath = defaultDatabasePath();
        if (!catalogDb.open(dbPath))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Database Error",
                                                   "Failed to open catalog database:\n" + juce::String(dbPath));
        }

        browserPanel.onAddRootRequested = [this]
        {
            handleAddRootClicked();
        };

        browserPanel.onCancelScanRequested = [this]
        {
            cancelScan();
        };

        browserPanel.onRescanAllRequested = [this]
        {
            handleRescanAllClicked();
        };

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
        };

        previewPanel.onStopRequested = [this]
        {
            audioEngine.stop();
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

        audioEngine.initialiseDeviceManager();
        restoreAudioDeviceSettings();
        restorePreviewSettings();
        refreshOutputDeviceTypeList();
        refreshOutputDeviceList();

        midiRouter.setMidiCallback([this](const juce::MidiMessage &message)
                                   { audioEngine.handleMidiMessage(message); });
        midiRouter.attachKeyboardState(previewPanel.getKeyboardState());

        const auto midiDevices = juce::MidiInput::getAvailableDevices();
        if (!midiDevices.isEmpty())
            midiRouter.enableDevice(midiDevices[0].identifier);

        refreshRoots();
        refreshResults();
        restoreLastSelection();
        restoreScanSummaryStatus();
        browserPanel.setScanInProgress(false);

        setSize(1200, 800);
    }

    MainComponent::~MainComponent() = default;

    void MainComponent::refreshRoots()
    {
        browserPanel.setRoots(catalogDb.allRoots());
    }

    void MainComponent::refreshResults(const std::string &query)
    {
        if (query.empty())
            resultsPanel.setResults(catalogDb.listRecentFiles(300));
        else
            resultsPanel.setResults(catalogDb.searchFiles(query + "*", 300));
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

    void MainComponent::persistPreviewPitch(double semitones)
    {
        catalogDb.setAppSetting("preview.pitchSemitones", juce::String(semitones).toStdString());
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
                    if (playWhenReady)
                        safeThis->audioEngine.play(); });
            },
            JobPriority::High});
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
            refreshResults();
            resultsPanel.selectFirstRowIfNoneSelected(); });
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();

        // Left panel – browser / roots tree (250 px wide)
        browserPanel.setBounds(area.removeFromLeft(250));

        // Bottom strip – preview controls (160 px tall)
        auto bottomArea = area.removeFromBottom(160);

        // Right portion of bottom – waveform (takes remaining width)
        // Left portion of bottom – preview controls (300 px)
        previewPanel.setBounds(bottomArea.removeFromLeft(300));
        waveformPanel.setBounds(bottomArea);

        // Center – results / search list
        resultsPanel.setBounds(area);
    }

} // namespace sw
