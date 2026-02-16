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
#include <optional>

namespace sw
{

    /// Top-level component that owns the main layout and sub-panels.
    class MainComponent final : public juce::Component,
                                private juce::Timer
    {
    public:
        MainComponent();
        ~MainComponent() override;

        void paint(juce::Graphics &g) override;
        void resized() override;

    private:
        void refreshRoots();
        void refreshResults(const std::string &query = "");
        void refreshOutputDeviceTypeList();
        void refreshOutputDeviceList();
        void refreshMidiInputDeviceList(bool forceRefresh = false);
        void applyMidiInputSelection(const juce::String &deviceIdentifier, bool persistSelection);
        void restoreAudioDeviceSettings();
        void restorePreviewSettings();
        void restoreMidiInputSettings();
        void restoreLastSelection();
        void restoreLayoutSettings();
        void restoreScanSummaryStatus();
        void persistAudioDeviceType(const juce::String &typeName);
        void persistAudioDeviceName(const juce::String &deviceName);
        void persistMidiInputSelection(const juce::String &deviceIdentifier);
        void persistLayoutSettings();
        void persistPreviewPitch(double semitones);
        void persistLastSelectedFile(const FileRecord &file);
        void persistScanSummaryStatus(const juce::String &statusText);
        void startRootScan(int64_t rootId,
                           const std::string &rootPath,
                           const juce::String &rootDisplayName,
                           std::function<void()> onCompleted = {});
        void handleRescanAllClicked();
        void cancelScan();
        void resetLayout();
        void handleAddRootClicked();
        void handleFileSelected(const FileRecord &file, bool playWhenReady, bool showIndexOnlyAlert);
        std::string rootPathForId(int64_t rootId);
        void timerCallback() override;
        void updateToolbarScanState(bool inProgress);

        class SplitterBar final : public juce::Component
        {
        public:
            enum class Orientation
            {
                vertical,
                horizontal
            };

            explicit SplitterBar(Orientation orientation);

            std::function<void(int deltaPixels)> onDragged;
            std::function<void()> onDragEnded;

            void paint(juce::Graphics &g) override;
            void mouseDown(const juce::MouseEvent &event) override;
            void mouseDrag(const juce::MouseEvent &event) override;
            void mouseUp(const juce::MouseEvent &event) override;

        private:
            Orientation orientation;
            juce::Point<int> lastScreenPosition;
        };

        // Owned sub-panels
        juce::Component toolbar;
        juce::DrawableButton addRootToolbarButton{"Add Root", juce::DrawableButton::ImageFitted};
        juce::DrawableButton rescanToolbarButton{"Rescan All", juce::DrawableButton::ImageFitted};
        juce::DrawableButton cancelScanToolbarButton{"Cancel Scan", juce::DrawableButton::ImageFitted};
        juce::DrawableButton resetLayoutToolbarButton{"Reset Layout", juce::DrawableButton::ImageFitted};
        juce::TooltipWindow tooltipWindow{this};

        BrowserPanel browserPanel;
        SplitterBar leftRightSplitter{SplitterBar::Orientation::vertical};
        ResultsPanel resultsPanel;
        SplitterBar resultsBottomSplitter{SplitterBar::Orientation::horizontal};
        WaveformPanel waveformPanel;
        PreviewPanel previewPanel;
        SplitterBar previewWaveformSplitter{SplitterBar::Orientation::vertical};

        float leftPanelRatio = 0.24f;
        float bottomPanelRatio = 0.24f;
        float previewPanelRatio = 0.35f;

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
        std::optional<int64_t> selectedRootFilterId;
        std::string currentSearchQuery;
        juce::String selectedMidiInputIdentifier;
        juce::StringArray lastKnownMidiInputIdentifiers;
        int midiDeviceRefreshCounter = 0;
        juce::String toolbarFeedbackText;
        int toolbarFeedbackTicksRemaining = 0;
        std::chrono::steady_clock::time_point scanStartTime{};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
    };

} // namespace sw
