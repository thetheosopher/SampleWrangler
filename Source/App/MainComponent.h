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
#include <atomic>
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
        bool keyPressed(const juce::KeyPress &key) override;

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
        void restoreThemeSettings();
        void restoreLastSelection();
        void restoreLayoutSettings();
        void restoreScanSummaryStatus();
        void persistAudioDeviceType(const juce::String &typeName);
        void persistAudioDeviceName(const juce::String &deviceName);
        void persistMidiInputSelection(const juce::String &deviceIdentifier);
        void persistLayoutSettings();
        void persistPreviewPitch(double semitones);
        void persistPreviewAutoPlayEnabled(bool enabled);
        void persistPreviewLoopEnabled(bool enabled);
        void persistPreviewStretchEnabled(bool enabled);
        void persistPreviewStretchHighQualityEnabled(bool enabled);
        void persistThemeMode(bool darkMode);
        void persistLastSelectedFile(const FileRecord &file);
        void persistScanSummaryStatus(const juce::String &statusText);
        void setScanStatusText(const juce::String &statusText);
        void updateWindowTitleForLoadedFile(const juce::String &fullPath);
        void startRootScan(int64_t rootId,
                           const std::string &rootPath,
                           const juce::String &rootDisplayName,
                           std::function<void()> onCompleted = {});
        void handleRescanSelectedClicked();
        void cancelScan();
        void handleOpenSourceInExplorerClicked();
        void handleDeleteRootClicked();
        void handleVacuumDatabaseClicked();
        void resetLayout();
        void handleAddRootClicked();
        void handleFileSelected(const FileRecord &file, bool playWhenReady, bool showIndexOnlyAlert);
        void setPreviewLoadingState(bool isLoading, uint64_t requestId);
        void applyEffectiveLoopPlaybackMode();
        void advanceAutoplaySelectionAndPlay();
        void updateWaveformLoopOverlay();
        std::string rootPathForId(int64_t rootId);
        void timerCallback() override;
        void updateToolbarScanState(bool inProgress);
        void applyThemeMode(bool darkMode, bool persist);

        class SplitterBar final : public juce::Component
        {
        public:
            enum class Orientation
            {
                vertical,
                horizontal
            };

            explicit SplitterBar(Orientation orientation);
            void setDarkMode(bool enabled);

            std::function<void(int deltaPixels)> onDragged;
            std::function<void()> onDragEnded;

            void paint(juce::Graphics &g) override;
            void mouseDown(const juce::MouseEvent &event) override;
            void mouseDrag(const juce::MouseEvent &event) override;
            void mouseUp(const juce::MouseEvent &event) override;

        private:
            Orientation orientation;
            juce::Point<int> lastScreenPosition;
            bool darkModeEnabled = true;
        };

        class TooltipLookAndFeel final : public juce::LookAndFeel_V4
        {
        public:
            void setDarkMode(bool enabled) { darkModeEnabled = enabled; }

            juce::Rectangle<int> getTooltipBounds(const juce::String &tipText,
                                                  juce::Point<int> screenPos,
                                                  juce::Rectangle<int> parentArea) override;
            void drawTooltip(juce::Graphics &g,
                             const juce::String &text,
                             int width,
                             int height) override;

        private:
            bool darkModeEnabled = true;
        };

        // Owned sub-panels
        juce::Component toolbar;
        juce::DrawableButton addRootToolbarButton{"Add Source", juce::DrawableButton::ImageFitted};
        juce::DrawableButton openSourceInExplorerToolbarButton{"Open Source In Explorer", juce::DrawableButton::ImageFitted};
        juce::DrawableButton deleteRootToolbarButton{"Delete Source", juce::DrawableButton::ImageFitted};
        juce::DrawableButton rescanToolbarButton{"Rescan Selected Source", juce::DrawableButton::ImageFitted};
        juce::DrawableButton cancelScanToolbarButton{"Cancel Scan", juce::DrawableButton::ImageFitted};
        juce::DrawableButton resetLayoutToolbarButton{"Reset Layout", juce::DrawableButton::ImageFitted};
        juce::DrawableButton vacuumDbToolbarButton{"Compress Database", juce::DrawableButton::ImageFitted};
        juce::DrawableButton themeToolbarButton{"Theme", juce::DrawableButton::ImageFitted};
        TooltipLookAndFeel tooltipLookAndFeel;
        juce::TooltipWindow tooltipWindow{this};

        BrowserPanel browserPanel;
        SplitterBar leftRightSplitter{SplitterBar::Orientation::vertical};
        ResultsPanel resultsPanel;
        SplitterBar resultsWaveformSplitter{SplitterBar::Orientation::horizontal};
        WaveformPanel waveformPanel;
        SplitterBar waveformBottomSplitter{SplitterBar::Orientation::horizontal};
        PreviewPanel previewPanel;
        SplitterBar previewKeyboardSplitter{SplitterBar::Orientation::vertical};
        juce::MidiKeyboardState keyboardState;
        juce::MidiKeyboardComponent keyboard{keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard};
        juce::ComboBox midiInputCombo;
        juce::StringArray midiInputDeviceIdentifiers;

        float leftPanelRatio = 0.24f;
        float waveformPanelRatio = 0.55f;
        float bottomPanelRatio = 0.24f;
        float previewPanelRatio = 0.45f;

        // Core subsystems (non-UI)
        CatalogDb catalogDb;
        JobQueue jobQueue{2};
        Scanner scanner{catalogDb, jobQueue};
        AudioEngine audioEngine;
        MidiInputRouter midiRouter;
        std::unique_ptr<juce::FileChooser> rootChooser;
        int scannedFilesCount = 0;
        bool scanInProgress = false;
        std::optional<int64_t> selectedRootFilterId;
        std::optional<FileRecord> currentSelectedFile;
        std::atomic<uint64_t> previewLoadRequestCounter{0};
        bool previewLoading = false;
        uint64_t previewLoadingRequestId = 0;
        std::string currentSearchQuery;
        juce::String selectedMidiInputIdentifier;
        juce::StringArray lastKnownMidiInputIdentifiers;
        int midiDeviceRefreshCounter = 0;
        juce::String toolbarFeedbackText;
        int toolbarFeedbackTicksRemaining = 0;
        juce::String scanStatusText{"Idle"};
        juce::String playbackPositionText{"00:00:00.000"};
        juce::Font playbackTimeFont{juce::FontOptions(20.0f)
                                        .withName(juce::Font::getDefaultMonospacedFontName())
                                        .withStyle("Regular")};
        bool darkModeEnabled = true;
        std::chrono::steady_clock::time_point scanStartTime{};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
    };

} // namespace sw
