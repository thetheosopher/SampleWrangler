#pragma once

#include <JuceHeader.h>
#include "UI/BrowserPanel.h"
#include "UI/ResultsPanel.h"
#include "UI/WaveformPanel.h"
#include "UI/PreviewPanel.h"
#include "Catalog/CatalogDb.h"
#include "Pipeline/JobQueue.h"
#include "Pipeline/Scanner.h"
#include "Audio/AudioEngine.h"
#include "Audio/MidiInputRouter.h"

#include <chrono>
#include <functional>
#include <memory>

namespace sw
{

    /// Top-level component that owns the main layout and sub-panels.
    class MainComponent final : public juce::Component
    {
    public:
        MainComponent();
        ~MainComponent() override;

        void resized() override;

    private:
        void refreshRoots();
        void refreshResults(const std::string &query = "");
        void refreshOutputDeviceTypeList();
        void refreshOutputDeviceList();
        void restoreAudioDeviceSettings();
        void restorePreviewSettings();
        void restoreLastSelection();
        void restoreScanSummaryStatus();
        void persistAudioDeviceType(const juce::String &typeName);
        void persistAudioDeviceName(const juce::String &deviceName);
        void persistPreviewPitch(double semitones);
        void persistLastSelectedFile(const FileRecord &file);
        void persistScanSummaryStatus(const juce::String &statusText);
        void startRootScan(int64_t rootId,
                           const std::string &rootPath,
                           const juce::String &rootDisplayName,
                           std::function<void()> onCompleted = {});
        void handleRescanAllClicked();
        void cancelScan();
        void handleAddRootClicked();
        void handleFileSelected(const FileRecord &file, bool playWhenReady, bool showIndexOnlyAlert);
        std::string rootPathForId(int64_t rootId);

        // Owned sub-panels
        BrowserPanel browserPanel;
        ResultsPanel resultsPanel;
        WaveformPanel waveformPanel;
        PreviewPanel previewPanel;

        // Core subsystems (non-UI)
        CatalogDb catalogDb;
        JobQueue jobQueue{2};
        Scanner scanner{catalogDb, jobQueue};
        AudioEngine audioEngine;
        MidiInputRouter midiRouter;
        std::unique_ptr<juce::FileChooser> rootChooser;
        int scannedFilesCount = 0;
        bool scanInProgress = false;
        bool rescanAllInProgress = false;
        int rescanCurrentRootIndex = 0;
        int rescanTotalRoots = 0;
        std::chrono::steady_clock::time_point scanStartTime{};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
    };

} // namespace sw
