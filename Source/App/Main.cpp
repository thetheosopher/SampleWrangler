/*
    SampleWrangler — Main application entry point.
    Uses JUCE JUCEApplication to create and manage the app lifecycle.
*/

#include <JuceHeader.h>
#include "MainComponent.h"
#include "BinaryData.h"
#include <chrono>
#include <ctime>

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
        class AboutContent final : public juce::Component
        {
        public:
            explicit AboutContent(const juce::String &version)
                : appVersion(version)
            {
            }

            void paint(juce::Graphics &g) override
            {
                const auto bounds = getLocalBounds().toFloat();
                const auto background = juce::Colour(0xfff5f7fb);
                const auto panel = juce::Colour(0xffffffff);
                const auto titleColour = juce::Colour(0xff1f2a33);
                const auto bodyColour = juce::Colour(0xff314252);
                const auto accent = juce::Colour(0xffc47a1b);

                g.fillAll(background);
                g.setColour(panel);
                g.fillRoundedRectangle(bounds.reduced(8.0f), 10.0f);
                g.setColour(juce::Colour(0xffccd7e4));
                g.drawRoundedRectangle(bounds.reduced(8.0f), 10.0f, 1.2f);

                auto content = getLocalBounds().reduced(24);

                juce::Path lasso;
                lasso.startNewSubPath(72.0f, 86.0f);
                lasso.cubicTo(22.0f, 48.0f, 48.0f, 12.0f, 96.0f, 16.0f);
                lasso.cubicTo(142.0f, 20.0f, 170.0f, 58.0f, 146.0f, 92.0f);
                lasso.cubicTo(122.0f, 124.0f, 70.0f, 126.0f, 52.0f, 98.0f);
                lasso.cubicTo(36.0f, 74.0f, 54.0f, 52.0f, 86.0f, 55.0f);
                lasso.cubicTo(121.0f, 58.0f, 130.0f, 90.0f, 104.0f, 100.0f);
                lasso.startNewSubPath(98.0f, 96.0f);
                lasso.cubicTo(124.0f, 118.0f, 142.0f, 130.0f, 178.0f, 144.0f);

                g.setColour(juce::Colour(0x33c47a1b));
                g.strokePath(lasso, juce::PathStrokeType(10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(accent);
                g.strokePath(lasso, juce::PathStrokeType(5.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                juce::Path lassoTip;
                lassoTip.addTriangle(178.0f, 144.0f, 165.0f, 146.0f, 171.0f, 132.0f);
                g.setColour(juce::Colour(0xff9a5f14));
                g.fillPath(lassoTip);

                content.removeFromLeft(190);

                g.setColour(titleColour);
                g.setFont(juce::FontOptions(32.0f).withStyle("Bold"));
                g.drawText("Sample Wrangler", content.removeFromTop(48), juce::Justification::centredLeft, false);

                g.setFont(juce::FontOptions(15.0f).withStyle("Regular"));
                g.setColour(bodyColour);
                g.drawText("Version " + appVersion, content.removeFromTop(24), juce::Justification::centredLeft, false);

                const int year = currentYear();
                g.drawText("\u00A9 " + juce::String(year) + " Michael A. McCloskey and GitHub Copilot", content.removeFromTop(24), juce::Justification::centredLeft, false);
                g.drawText("Licensed under the MIT License", content.removeFromTop(22), juce::Justification::centredLeft, false);

                content.removeFromTop(8);

                g.setColour(titleColour);
                g.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
                g.drawText("Features", content.removeFromTop(24), juce::Justification::centredLeft, false);

                g.setColour(bodyColour);
                g.setFont(juce::FontOptions(14.0f));
                juce::String featuresText =
                    "\u2022 Source library management and incremental scanning\n"
                    "\u2022 Fast catalog search with metadata-rich results\n"
                    "\u2022 Waveform and spectrogram preview display modes\n"
                    "\u2022 Real-time preview playback with MIDI/keyboard pitch control\n"
                    "\u2022 Loop and time-stretch preview controls";
                g.drawFittedText(featuresText,
                                 content.removeFromTop(108),
                                 juce::Justification::topLeft,
                                 7);
            }

        private:
            static int currentYear()
            {
                const auto now = std::chrono::system_clock::now();
                const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
                std::tm localTime{};
#if JUCE_WINDOWS
                localtime_s(&localTime, &nowTime);
#else
                localTime = *std::localtime(&nowTime);
#endif
                return 1900 + localTime.tm_year;
            }

            juce::String appVersion;
        };

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
            toFront(true);

#if JUCE_WINDOWS
            if (auto *nativeHandle = getWindowHandle())
                installAboutSystemMenuItem(static_cast<HWND>(nativeHandle));
#endif
        }

        ~MainWindow() override
        {
#if JUCE_WINDOWS
            if (windowHandle != nullptr && originalWndProc != nullptr)
            {
                ::SetWindowLongPtrW(windowHandle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc));
                ::RemovePropW(windowHandle, kWindowPropName);
            }
#endif
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
#if JUCE_WINDOWS
        static constexpr UINT kAboutSystemMenuId = 0x1FF0;
        static constexpr wchar_t kWindowPropName[] = L"SampleWranglerMainWindowPtr";

        void installAboutSystemMenuItem(HWND hwnd)
        {
            if (hwnd == nullptr || originalWndProc != nullptr)
                return;

            if (auto menu = ::GetSystemMenu(hwnd, FALSE))
            {
                ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                ::AppendMenuW(menu, MF_STRING, kAboutSystemMenuId, L"About Schema Wrangler...");
            }

            windowHandle = hwnd;
            ::SetPropW(windowHandle, kWindowPropName, reinterpret_cast<HANDLE>(this));
            originalWndProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(windowHandle, GWLP_WNDPROC,
                                                                            reinterpret_cast<LONG_PTR>(&MainWindow::windowProcRouter)));
        }

        static LRESULT CALLBACK windowProcRouter(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto *self = reinterpret_cast<MainWindow *>(::GetPropW(hwnd, kWindowPropName));
            if (self == nullptr)
                return ::DefWindowProcW(hwnd, message, wParam, lParam);

            return self->handleWindowMessage(hwnd, message, wParam, lParam);
        }

        LRESULT handleWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_SYSCOMMAND)
            {
                const UINT command = static_cast<UINT>(wParam & 0xFFF0);
                if (command == kAboutSystemMenuId)
                {
                    showAboutBox();
                    return 0;
                }
            }

            return ::CallWindowProcW(originalWndProc, hwnd, message, wParam, lParam);
        }
#endif

        void showAboutBox()
        {
            juce::DialogWindow::LaunchOptions options;
            options.dialogTitle = "About Sample Wrangler";
            options.content.setOwned(new AboutContent(JUCE_APPLICATION_VERSION_STRING));
            options.content->setSize(760, 460);
            options.componentToCentreAround = this;
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = false;

            if (auto *dialog = options.launchAsync())
                dialog->setAlwaysOnTop(true);
        }

#if JUCE_WINDOWS
        HWND windowHandle = nullptr;
        WNDPROC originalWndProc = nullptr;
#endif

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION(SampleWranglerApplication)
