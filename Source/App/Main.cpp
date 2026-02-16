/*
    SampleWrangler — Main application entry point.
    Uses JUCE JUCEApplication to create and manage the app lifecycle.
*/

#include <JuceHeader.h>
#include "MainComponent.h"
#include "BinaryData.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

//==============================================================================
class SampleWranglerApplication final : public juce::JUCEApplication
{
public:
    SampleWranglerApplication() = default;

    const juce::String getApplicationName() override { return "Sample Wrangler"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    //==========================================================================
    void initialise(const juce::String & /*commandLine*/) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String & /*commandLine*/) override {}

    //==========================================================================
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String &name)
            : DocumentWindow(name,
                             juce::Desktop::getInstance()
                                 .getDefaultLookAndFeel()
                                 .findColour(ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons)
        {
            constexpr int kMainIconResourceId = 101;

            setUsingNativeTitleBar(true);

            const auto appIcon = juce::ImageFileFormat::loadFrom(BinaryData::SampleWrangler_ico,
                                                                 BinaryData::SampleWrangler_icoSize);
            if (appIcon.isValid())
                setIcon(appIcon);

#if JUCE_WINDOWS
            if (auto *nativeHandle = getWindowHandle())
            {
                auto hwnd = static_cast<HWND>(nativeHandle);
                auto module = reinterpret_cast<HINSTANCE>(::GetModuleHandleW(nullptr));

                if (auto *bigIcon = reinterpret_cast<HICON>(::LoadImageW(module, MAKEINTRESOURCEW(kMainIconResourceId), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR)))
                    ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));

                if (auto *smallIcon = reinterpret_cast<HICON>(::LoadImageW(module, MAKEINTRESOURCEW(kMainIconResourceId), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR)))
                    ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
            }
#endif

            setContentOwned(new sw::MainComponent(), true);

#if JUCE_IOS || JUCE_ANDROID
            setFullScreen(true);
#else
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
#endif
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION(SampleWranglerApplication)
