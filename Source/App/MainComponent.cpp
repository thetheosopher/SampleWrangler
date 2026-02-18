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
        bool hasEmbeddedLoopMetadata(const FileRecord &file)
        {
            if (!file.loopType.has_value())
                return false;

            return *file.loopType == "acidized" || *file.loopType == "apple-loop";
        }

        constexpr int kToolbarHeight = 48;
        constexpr int kStatusBarHeight = 30;
        constexpr int kUiTimerHz = 60;
        constexpr int kMidiDeviceRefreshIntervalTicks = kUiTimerHz;
        constexpr int kSplitterThickness = 5;
        constexpr int kMinLeftPanelWidth = 180;
        constexpr int kMinRightPanelWidth = 320;
        constexpr int kMinResultsHeight = 120;
        constexpr int kMinWaveformHeight = 120;
        constexpr int kMinBottomHeight = 96;
        constexpr int kMinPreviewWidth = 220;
        constexpr int kMinKeyboardWidth = 200;
        constexpr float kDefaultLeftPanelRatio = 0.24f;
        constexpr float kDefaultWaveformPanelRatio = 0.435f;
        constexpr float kDefaultBottomPanelRatio = 0.13f;
        constexpr float kDefaultPreviewPanelRatio = 0.45f;

        juce::String formatClockHmsMs(double seconds)
        {
            const int totalMilliseconds = juce::jmax(0, static_cast<int>(std::round(seconds * 1000.0)));
            const int hours = totalMilliseconds / 3600000;
            const int minutes = (totalMilliseconds / 60000) % 60;
            const int secs = (totalMilliseconds / 1000) % 60;
            const int millis = totalMilliseconds % 1000;
            return juce::String::formatted("%02d:%02d:%02d.%03d", hours, minutes, secs, millis);
        }
    }

    namespace
    {
        std::unique_ptr<juce::Drawable> createFolderIcon(const juce::Colour colour)
        {
            constexpr int iconSizePx = 96;
            constexpr float scale = static_cast<float>(iconSizePx) / 24.0f;

            juce::Image image(juce::Image::ARGB, iconSizePx, iconSizePx, true);
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale));

            juce::Path folderBack;
            folderBack.addRoundedRectangle(2.0f, 6.2f, 20.0f, 12.4f, 2.1f);

            juce::Path folderTab;
            folderTab.addRoundedRectangle(4.0f, 3.2f, 7.8f, 4.2f, 1.2f);

            juce::Path folderFront;
            folderFront.startNewSubPath(2.2f, 9.5f);
            folderFront.lineTo(22.0f, 9.5f);
            folderFront.lineTo(18.6f, 20.0f);
            folderFront.lineTo(4.4f, 20.0f);
            folderFront.closeSubPath();

            g.setColour(juce::Colour(0x1a000000));
            g.fillRoundedRectangle(2.2f, 6.7f, 20.0f, 13.2f, 2.1f);

            g.setColour(juce::Colour(0xffd8a64a));
            g.fillPath(folderBack);

            g.setColour(juce::Colour(0xfff6d178));
            g.fillPath(folderTab);

            g.setColour(juce::Colour(0xfff1c258));
            g.fillPath(folderFront);

            juce::Path flapHighlight;
            flapHighlight.startNewSubPath(3.4f, 10.6f);
            flapHighlight.lineTo(21.0f, 10.6f);
            flapHighlight.lineTo(20.5f, 12.1f);
            flapHighlight.lineTo(3.0f, 12.1f);
            flapHighlight.closeSubPath();
            g.setColour(juce::Colour(0x55ffffff));
            g.fillPath(flapHighlight);

            const auto tintOverlay = colour.getPerceivedBrightness() < 0.45f
                                         ? colour.withAlpha(0.22f)
                                         : colour.withAlpha(0.14f);
            g.setColour(tintOverlay);
            g.strokePath(folderFront, juce::PathStrokeType(0.65f));

            g.setColour(juce::Colour(0xc06b4a1f));
            g.strokePath(folderBack, juce::PathStrokeType(0.9f));
            g.strokePath(folderFront, juce::PathStrokeType(0.9f));
            g.strokePath(folderTab, juce::PathStrokeType(0.85f));

            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(image);
            return drawable;
        }

        std::unique_ptr<juce::Drawable> createRescanIcon(const juce::Colour colour)
        {
            juce::ignoreUnused(colour);
            constexpr int iconSizePx = 96;
            constexpr float scale = static_cast<float>(iconSizePx) / 24.0f;

            juce::Image image(juce::Image::ARGB, iconSizePx, iconSizePx, true);
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale));

            constexpr float arcRadius = 7.0f;
            constexpr float arcEndAngle = juce::MathConstants<float>::pi * 1.89f;

            juce::Path circularArrow;
            circularArrow.addCentredArc(12.0f, 12.0f, arcRadius, arcRadius, 0.0f,
                                        juce::MathConstants<float>::pi * 0.23f,
                                        arcEndAngle,
                                        true);

            const float arcEndX = 12.0f + arcRadius * std::cos(arcEndAngle);
            const float arcEndY = 12.0f + arcRadius * std::sin(arcEndAngle);

            const juce::Point<float> tangent(-std::sin(arcEndAngle), std::cos(arcEndAngle));
            const juce::Point<float> normal(-tangent.y, tangent.x);

            constexpr float arrowTipOffset = 3.8f * 1.5f;
            constexpr float arrowTailOffset = 0.9f * 1.5f;
            constexpr float arrowHalfWidth = 2.9f * 1.5f;

            const auto arcEnd = juce::Point<float>(arcEndX, arcEndY);
            const auto baseCenter = arcEnd + tangent * arrowTailOffset;
            const auto tip = arcEnd - tangent * arrowTipOffset;
            const auto baseA = baseCenter + normal * arrowHalfWidth;
            const auto baseB = baseCenter - normal * arrowHalfWidth;

            juce::Path arrowHead;
            arrowHead.addTriangle(tip.x, tip.y,
                                  baseA.x, baseA.y,
                                  baseB.x, baseB.y);

            juce::Path arrowNeck;
            arrowNeck.startNewSubPath((arcEnd + tangent * 0.15f).x, (arcEnd + tangent * 0.15f).y);
            arrowNeck.lineTo((baseCenter - tangent * 0.9f).x, (baseCenter - tangent * 0.9f).y);

            juce::Path arrowHeadBackdrop;
            arrowHeadBackdrop.addTriangle((tip - tangent * 0.35f).x, (tip - tangent * 0.35f).y,
                                          (baseA + tangent * 0.2f + normal * 0.35f).x, (baseA + tangent * 0.2f + normal * 0.35f).y,
                                          (baseB + tangent * 0.2f - normal * 0.35f).x, (baseB + tangent * 0.2f - normal * 0.35f).y);

            g.setColour(juce::Colour(0xff2f8dff));
            g.strokePath(circularArrow, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(juce::Colour(0xff1b5e8f));
            g.strokePath(circularArrow, juce::PathStrokeType(0.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Opaque backing ensures the head reads clearly over the ring.
            g.setColour(juce::Colours::white);
            g.fillPath(arrowHeadBackdrop);

            g.setColour(juce::Colour(0xff2abf85));
            g.fillPath(arrowHead);
            g.strokePath(arrowNeck, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(juce::Colour(0xff1a8b66));
            g.strokePath(arrowHead, juce::PathStrokeType(0.65f));

            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(image);
            return drawable;
        }

        std::unique_ptr<juce::Drawable> createCancelIcon(const juce::Colour colour)
        {
            constexpr int iconSizePx = 96;
            constexpr float scale = static_cast<float>(iconSizePx) / 24.0f;

            juce::Image image(juce::Image::ARGB, iconSizePx, iconSizePx, true);
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale));

            juce::Path ring;
            ring.addEllipse(3.5f, 3.5f, 17.0f, 17.0f);

            juce::Path innerDisc;
            innerDisc.addEllipse(5.7f, 5.7f, 12.6f, 12.6f);

            juce::Path cross;
            cross.startNewSubPath(8.2f, 8.2f);
            cross.lineTo(15.8f, 15.8f);
            cross.startNewSubPath(15.8f, 8.2f);
            cross.lineTo(8.2f, 15.8f);

            g.setColour(juce::Colour(0xffda4f4f));
            g.fillPath(ring);

            g.setColour(juce::Colour(0xffbe3d3d));
            g.fillPath(innerDisc);

            g.setColour(juce::Colour(0x55ffffff));
            g.fillEllipse(6.4f, 6.3f, 6.8f, 3.0f);

            g.setColour(juce::Colours::white.withAlpha(0.96f));
            g.strokePath(cross, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(juce::Colour(0xb06f2e2e));
            g.strokePath(ring, juce::PathStrokeType(0.85f));

            const auto tintOverlay = colour.getPerceivedBrightness() < 0.45f
                                         ? colour.withAlpha(0.20f)
                                         : colour.withAlpha(0.10f);
            g.setColour(tintOverlay);
            g.strokePath(ring, juce::PathStrokeType(0.6f));

            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(image);
            return drawable;
        }

        std::unique_ptr<juce::Drawable> createResetLayoutIcon(const juce::Colour colour)
        {
            constexpr int iconSizePx = 96;
            constexpr float scale = static_cast<float>(iconSizePx) / 24.0f;

            juce::Image image(juce::Image::ARGB, iconSizePx, iconSizePx, true);
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale));

            juce::Path tileA;
            tileA.addRoundedRectangle(3.1f, 3.1f, 7.6f, 7.6f, 1.0f);
            juce::Path tileB;
            tileB.addRoundedRectangle(13.3f, 3.1f, 7.6f, 7.6f, 1.0f);
            juce::Path tileC;
            tileC.addRoundedRectangle(3.1f, 13.3f, 7.6f, 7.6f, 1.0f);
            juce::Path tileD;
            tileD.addRoundedRectangle(13.3f, 13.3f, 7.6f, 7.6f, 1.0f);

            g.setColour(juce::Colour(0xff5da7ff));
            g.fillPath(tileA);
            g.setColour(juce::Colour(0xff7fb7ff));
            g.fillPath(tileB);
            g.setColour(juce::Colour(0xff3f95ff));
            g.fillPath(tileC);
            g.setColour(juce::Colour(0xff65c39d));
            g.fillPath(tileD);

            g.setColour(juce::Colour(0x45ffffff));
            g.fillRoundedRectangle(3.8f, 3.8f, 15.4f, 2.2f, 0.7f);

            juce::Path resetArrow;
            resetArrow.addCentredArc(12.0f, 12.0f, 6.0f, 6.0f, 0.0f,
                                     juce::MathConstants<float>::pi * 0.14f,
                                     juce::MathConstants<float>::pi * 1.16f,
                                     true);
            juce::Path resetHead;
            resetHead.addTriangle(18.25f, 8.0f, 21.2f, 10.2f, 17.45f, 11.4f);

            g.setColour(juce::Colours::white.withAlpha(0.96f));
            g.strokePath(resetArrow, juce::PathStrokeType(1.45f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillPath(resetHead);

            g.setColour(juce::Colour(0xb02c5f9d));
            g.strokePath(tileA, juce::PathStrokeType(0.72f));
            g.strokePath(tileB, juce::PathStrokeType(0.72f));
            g.strokePath(tileC, juce::PathStrokeType(0.72f));
            g.strokePath(tileD, juce::PathStrokeType(0.72f));

            const auto tintOverlay = colour.getPerceivedBrightness() < 0.45f
                                         ? colour.withAlpha(0.20f)
                                         : colour.withAlpha(0.10f);
            g.setColour(tintOverlay);
            g.strokePath(resetArrow, juce::PathStrokeType(0.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(image);
            return drawable;
        }

        std::unique_ptr<juce::Drawable> createVacuumIcon(const juce::Colour colour)
        {
            constexpr int iconSizePx = 96;
            constexpr float scale = static_cast<float>(iconSizePx) / 24.0f;

            juce::Image image(juce::Image::ARGB, iconSizePx, iconSizePx, true);
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale));

            juce::Path base;
            base.addRoundedRectangle(3.0f, 16.5f, 18.0f, 4.0f, 1.0f);

            juce::Path fixedJaw;
            fixedJaw.addRoundedRectangle(4.0f, 8.0f, 4.0f, 8.5f, 0.8f);

            juce::Path movingJaw;
            movingJaw.addRoundedRectangle(15.0f, 8.0f, 4.0f, 8.5f, 0.8f);

            juce::Path screwBar;
            screwBar.addRoundedRectangle(8.0f, 11.2f, 7.2f, 2.2f, 0.9f);

            juce::Path handle;
            handle.startNewSubPath(11.2f, 10.0f);
            handle.lineTo(11.2f, 7.0f);
            handle.startNewSubPath(9.4f, 8.5f);
            handle.lineTo(13.0f, 8.5f);

            juce::Path compressedBlock;
            compressedBlock.addRoundedRectangle(8.8f, 9.1f, 1.8f, 6.2f, 0.45f);

            g.setColour(juce::Colour(0xff5f7f99));
            g.fillPath(base);
            g.setColour(juce::Colour(0xff6d8ea8));
            g.fillPath(fixedJaw);
            g.setColour(juce::Colour(0xff7ba1bf));
            g.fillPath(movingJaw);
            g.setColour(juce::Colour(0xff3f89d6));
            g.fillPath(screwBar);

            g.setColour(juce::Colour(0xff61d091));
            g.fillPath(compressedBlock);

            g.setColour(juce::Colour(0x66ffffff));
            g.fillRoundedRectangle(4.2f, 16.9f, 11.8f, 1.0f, 0.4f);

            g.setColour(juce::Colour(0xff3b5a72));
            g.strokePath(handle, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(juce::Colour(0xb02f4659));
            g.strokePath(base, juce::PathStrokeType(0.75f));
            g.strokePath(fixedJaw, juce::PathStrokeType(0.7f));
            g.strokePath(movingJaw, juce::PathStrokeType(0.7f));
            g.strokePath(screwBar, juce::PathStrokeType(0.65f));

            const auto tintOverlay = colour.getPerceivedBrightness() < 0.45f
                                         ? colour.withAlpha(0.20f)
                                         : colour.withAlpha(0.10f);
            g.setColour(tintOverlay);
            g.strokePath(base, juce::PathStrokeType(0.55f));

            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(image);
            return drawable;
        }

        std::unique_ptr<juce::Drawable> createDeleteIcon(const juce::Colour colour)
        {
            constexpr int iconSizePx = 96;
            constexpr float scale = static_cast<float>(iconSizePx) / 24.0f;

            juce::Image image(juce::Image::ARGB, iconSizePx, iconSizePx, true);
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale));

            juce::Path lid;
            lid.addRoundedRectangle(5.0f, 6.0f, 14.0f, 2.4f, 0.8f);

            juce::Path handle;
            handle.addRoundedRectangle(9.0f, 4.0f, 6.0f, 1.8f, 0.7f);

            juce::Path body;
            body.addRoundedRectangle(6.4f, 8.4f, 11.2f, 11.2f, 1.7f);

            g.setColour(juce::Colour(0x22000000));
            g.fillRoundedRectangle(6.6f, 9.1f, 11.2f, 11.4f, 1.8f);

            g.setColour(juce::Colour(0xffe15757));
            g.fillPath(body);

            g.setColour(juce::Colour(0xffcc4545));
            g.fillPath(lid);

            g.setColour(juce::Colour(0xfff06b6b));
            g.fillPath(handle);

            g.setColour(juce::Colour(0x55ffffff));
            g.fillRoundedRectangle(7.3f, 9.2f, 2.0f, 9.2f, 0.8f);

            g.setColour(juce::Colour(0x40ffffff));
            g.fillRoundedRectangle(10.6f, 10.0f, 0.9f, 8.1f, 0.4f);
            g.fillRoundedRectangle(12.6f, 10.0f, 0.9f, 8.1f, 0.4f);

            g.setColour(juce::Colour(0xc0842f2f));
            g.strokePath(body, juce::PathStrokeType(0.85f));
            g.strokePath(lid, juce::PathStrokeType(0.85f));
            g.strokePath(handle, juce::PathStrokeType(0.75f));

            const auto tintOverlay = colour.getPerceivedBrightness() < 0.45f
                                         ? colour.withAlpha(0.25f)
                                         : colour.withAlpha(0.14f);
            g.setColour(tintOverlay);
            g.strokePath(body, juce::PathStrokeType(0.6f));

            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(image);
            return drawable;
        }

        std::unique_ptr<juce::Drawable> createExplorerIcon(const juce::Colour colour)
        {
            constexpr int iconSizePx = 96;
            constexpr float scale = static_cast<float>(iconSizePx) / 24.0f;

            juce::Image image(juce::Image::ARGB, iconSizePx, iconSizePx, true);
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale));

            juce::Path folderBack;
            folderBack.addRoundedRectangle(2.0f, 6.4f, 15.6f, 11.8f, 1.9f);

            juce::Path folderTab;
            folderTab.addRoundedRectangle(3.8f, 3.5f, 6.8f, 3.7f, 1.1f);

            juce::Path folderFront;
            folderFront.startNewSubPath(2.2f, 9.4f);
            folderFront.lineTo(18.0f, 9.4f);
            folderFront.lineTo(15.2f, 19.4f);
            folderFront.lineTo(4.0f, 19.4f);
            folderFront.closeSubPath();

            g.setColour(juce::Colour(0x1a000000));
            g.fillRoundedRectangle(2.3f, 6.9f, 15.6f, 12.5f, 2.0f);

            g.setColour(juce::Colour(0xffd6a149));
            g.fillPath(folderBack);
            g.setColour(juce::Colour(0xfff2cc73));
            g.fillPath(folderTab);
            g.setColour(juce::Colour(0xffeebe56));
            g.fillPath(folderFront);

            juce::Path flapHighlight;
            flapHighlight.startNewSubPath(3.1f, 10.5f);
            flapHighlight.lineTo(17.2f, 10.5f);
            flapHighlight.lineTo(16.8f, 11.9f);
            flapHighlight.lineTo(2.8f, 11.9f);
            flapHighlight.closeSubPath();
            g.setColour(juce::Colour(0x55ffffff));
            g.fillPath(flapHighlight);

            juce::Path explorerWindow;
            explorerWindow.addRoundedRectangle(14.0f, 5.0f, 7.6f, 7.6f, 1.2f);
            g.setColour(juce::Colour(0xff4ea3ff));
            g.fillPath(explorerWindow);

            g.setColour(juce::Colour(0x55ffffff));
            g.fillRoundedRectangle(14.4f, 5.5f, 6.8f, 2.0f, 0.8f);

            juce::Path openArrow;
            openArrow.startNewSubPath(15.5f, 10.6f);
            openArrow.lineTo(20.0f, 6.1f);
            openArrow.startNewSubPath(17.3f, 6.1f);
            openArrow.lineTo(20.0f, 6.1f);
            openArrow.lineTo(20.0f, 8.8f);
            g.setColour(juce::Colours::white.withAlpha(0.95f));
            g.strokePath(openArrow, juce::PathStrokeType(1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(juce::Colour(0xc0476ea4));
            g.strokePath(explorerWindow, juce::PathStrokeType(0.85f));
            g.setColour(juce::Colour(0xc06b4a1f));
            g.strokePath(folderBack, juce::PathStrokeType(0.85f));
            g.strokePath(folderFront, juce::PathStrokeType(0.85f));
            g.strokePath(folderTab, juce::PathStrokeType(0.8f));

            const auto tintOverlay = colour.getPerceivedBrightness() < 0.45f
                                         ? colour.withAlpha(0.24f)
                                         : colour.withAlpha(0.14f);
            g.setColour(tintOverlay);
            g.strokePath(folderFront, juce::PathStrokeType(0.6f));

            // Repaint the Explorer badge on top so its background remains fully opaque.
            g.setColour(juce::Colour(0xff4ea3ff));
            g.fillPath(explorerWindow);
            g.setColour(juce::Colour(0x55ffffff));
            g.fillRoundedRectangle(14.4f, 5.5f, 6.8f, 2.0f, 0.8f);
            g.setColour(juce::Colours::white.withAlpha(0.95f));
            g.strokePath(openArrow, juce::PathStrokeType(1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(juce::Colour(0xc0476ea4));
            g.strokePath(explorerWindow, juce::PathStrokeType(0.85f));

            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(image);
            return drawable;
        }

        std::unique_ptr<juce::Drawable> createThemeIcon(const juce::Colour colour, bool showSun)
        {
            constexpr int iconSizePx = 96;
            constexpr float scale = static_cast<float>(iconSizePx) / 24.0f;

            juce::Image image(juce::Image::ARGB, iconSizePx, iconSizePx, true);
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale));

            if (showSun)
            {
                juce::Path sunCore;
                sunCore.addEllipse(7.2f, 7.2f, 9.6f, 9.6f);

                juce::Path rays;
                rays.startNewSubPath(12.0f, 2.8f);
                rays.lineTo(12.0f, 5.5f);
                rays.startNewSubPath(12.0f, 18.5f);
                rays.lineTo(12.0f, 21.2f);
                rays.startNewSubPath(2.8f, 12.0f);
                rays.lineTo(5.5f, 12.0f);
                rays.startNewSubPath(18.5f, 12.0f);
                rays.lineTo(21.2f, 12.0f);
                rays.startNewSubPath(5.3f, 5.3f);
                rays.lineTo(7.2f, 7.2f);
                rays.startNewSubPath(16.8f, 16.8f);
                rays.lineTo(18.7f, 18.7f);
                rays.startNewSubPath(16.8f, 7.2f);
                rays.lineTo(18.7f, 5.3f);
                rays.startNewSubPath(5.3f, 18.7f);
                rays.lineTo(7.2f, 16.8f);

                g.setColour(juce::Colour(0xffffc84e));
                g.fillPath(sunCore);
                g.setColour(juce::Colour(0x65ffffff));
                g.fillEllipse(8.5f, 8.3f, 5.2f, 2.3f);

                g.setColour(juce::Colour(0xffffaf35));
                g.strokePath(rays, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(juce::Colour(0xb07d5a17));
                g.strokePath(sunCore, juce::PathStrokeType(0.75f));
            }
            else
            {
                juce::Path moonCrescent;
                moonCrescent.startNewSubPath(16.8f, 3.4f);
                moonCrescent.cubicTo(8.6f, 3.7f, 4.7f, 8.0f, 4.6f, 12.0f);
                moonCrescent.cubicTo(4.7f, 16.0f, 8.6f, 20.3f, 16.8f, 20.6f);
                moonCrescent.cubicTo(13.0f, 18.1f, 11.3f, 15.3f, 11.2f, 12.0f);
                moonCrescent.cubicTo(11.3f, 8.7f, 13.0f, 5.9f, 16.8f, 3.4f);
                moonCrescent.closeSubPath();

                g.setColour(colour);
                g.fillPath(moonCrescent);
            }

            const auto tintOverlay = colour.getPerceivedBrightness() < 0.45f
                                         ? colour.withAlpha(0.20f)
                                         : colour.withAlpha(0.10f);
            g.setColour(tintOverlay);
            if (showSun)
                g.drawEllipse(4.2f, 4.2f, 15.6f, 15.6f, 0.55f);

            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(image);
            return drawable;
        }
    }

    MainComponent::MainComponent()
    {
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(true);
        tooltipWindow.setLookAndFeel(&tooltipLookAndFeel);

        addAndMakeVisible(toolbar);
        addAndMakeVisible(addRootToolbarButton);
        addAndMakeVisible(openSourceInExplorerToolbarButton);
        addAndMakeVisible(deleteRootToolbarButton);
        addAndMakeVisible(rescanToolbarButton);
        addAndMakeVisible(cancelScanToolbarButton);
        addAndMakeVisible(resetLayoutToolbarButton);
        addAndMakeVisible(vacuumDbToolbarButton);
        addAndMakeVisible(themeToolbarButton);
        addAndMakeVisible(browserPanel);
        addAndMakeVisible(leftRightSplitter);
        addAndMakeVisible(resultsPanel);
        addAndMakeVisible(resultsWaveformSplitter);
        addAndMakeVisible(waveformPanel);
        addAndMakeVisible(waveformBottomSplitter);
        addAndMakeVisible(previewPanel);
        addAndMakeVisible(previewKeyboardSplitter);
        addAndMakeVisible(keyboard);

        keyboard.setAvailableRange(36, 96); // C2–C7 range for preview

        midiInputCombo.setTextWhenNoChoicesAvailable("No MIDI inputs");
        midiInputCombo.onChange = [this]
        {
            const int index = midiInputCombo.getSelectedItemIndex();
            if (index >= 0 && index < midiInputDeviceIdentifiers.size())
            {
                applyMidiInputSelection(midiInputDeviceIdentifiers[index], true);
            }
            else
            {
                applyMidiInputSelection({}, true);
            }
        };
        addAndMakeVisible(midiInputCombo);

        leftRightSplitter.onDragged = [this](int deltaPixels)
        {
            auto content = getLocalBounds();
            content.removeFromTop(kToolbarHeight);
            content.removeFromBottom(kStatusBarHeight);

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

        resultsWaveformSplitter.onDragged = [this](int deltaPixels)
        {
            auto content = getLocalBounds();
            content.removeFromTop(kToolbarHeight);
            content.removeFromBottom(kStatusBarHeight);

            const int totalWidth = content.getWidth() - kSplitterThickness;
            if (totalWidth <= (kMinLeftPanelWidth + kMinRightPanelWidth))
                return;

            int leftWidth = static_cast<int>(leftPanelRatio * static_cast<float>(totalWidth));
            leftWidth = juce::jlimit(kMinLeftPanelWidth, totalWidth - kMinRightPanelWidth, leftWidth);

            juce::Rectangle<int> rightArea = content;
            rightArea.removeFromLeft(leftWidth + kSplitterThickness);

            const int totalHeight = rightArea.getHeight() - kSplitterThickness * 2;
            if (totalHeight <= (kMinResultsHeight + kMinWaveformHeight + kMinBottomHeight))
                return;

            int currentWaveformHeight = static_cast<int>(waveformPanelRatio * static_cast<float>(totalHeight));
            int nextWaveformHeight = currentWaveformHeight - deltaPixels;
            nextWaveformHeight = juce::jlimit(kMinWaveformHeight, totalHeight - kMinResultsHeight - kMinBottomHeight, nextWaveformHeight);

            waveformPanelRatio = static_cast<float>(nextWaveformHeight) / static_cast<float>(totalHeight);
            resized();
        };
        resultsWaveformSplitter.onDragEnded = [this]
        {
            persistLayoutSettings();
        };

        waveformBottomSplitter.onDragged = [this](int deltaPixels)
        {
            auto content = getLocalBounds();
            content.removeFromTop(kToolbarHeight);
            content.removeFromBottom(kStatusBarHeight);

            const int totalWidth = content.getWidth() - kSplitterThickness;
            if (totalWidth <= (kMinLeftPanelWidth + kMinRightPanelWidth))
                return;

            int leftWidth = static_cast<int>(leftPanelRatio * static_cast<float>(totalWidth));
            leftWidth = juce::jlimit(kMinLeftPanelWidth, totalWidth - kMinRightPanelWidth, leftWidth);

            juce::Rectangle<int> rightArea = content;
            rightArea.removeFromLeft(leftWidth + kSplitterThickness);

            const int totalHeight = rightArea.getHeight() - kSplitterThickness * 2;
            if (totalHeight <= (kMinResultsHeight + kMinWaveformHeight + kMinBottomHeight))
                return;

            int currentBottomHeight = static_cast<int>(bottomPanelRatio * static_cast<float>(totalHeight));
            int nextBottomHeight = currentBottomHeight - deltaPixels;
            nextBottomHeight = juce::jlimit(kMinBottomHeight, totalHeight - kMinResultsHeight - kMinWaveformHeight, nextBottomHeight);

            bottomPanelRatio = static_cast<float>(nextBottomHeight) / static_cast<float>(totalHeight);
            resized();
        };
        waveformBottomSplitter.onDragEnded = [this]
        {
            persistLayoutSettings();
        };

        previewKeyboardSplitter.onDragged = [this](int deltaPixels)
        {
            auto content = getLocalBounds();
            content.removeFromTop(kToolbarHeight);
            content.removeFromBottom(kStatusBarHeight);

            const int totalWidth = content.getWidth() - kSplitterThickness;
            if (totalWidth <= (kMinLeftPanelWidth + kMinRightPanelWidth))
                return;

            int leftWidth = static_cast<int>(leftPanelRatio * static_cast<float>(totalWidth));
            leftWidth = juce::jlimit(kMinLeftPanelWidth, totalWidth - kMinRightPanelWidth, leftWidth);

            juce::Rectangle<int> rightArea = content;
            rightArea.removeFromLeft(leftWidth + kSplitterThickness);

            const int totalHeight = rightArea.getHeight() - kSplitterThickness * 2;
            if (totalHeight <= (kMinResultsHeight + kMinWaveformHeight + kMinBottomHeight))
                return;

            int bottomHeight = static_cast<int>(bottomPanelRatio * static_cast<float>(totalHeight));
            bottomHeight = juce::jlimit(kMinBottomHeight, totalHeight - kMinResultsHeight - kMinWaveformHeight, bottomHeight);

            juce::Rectangle<int> bottomArea = rightArea;
            bottomArea.removeFromTop(totalHeight - bottomHeight);

            const int splitWidth = bottomArea.getWidth() - kSplitterThickness;
            if (splitWidth <= (kMinPreviewWidth + kMinKeyboardWidth))
                return;

            int currentPreviewWidth = static_cast<int>(previewPanelRatio * static_cast<float>(splitWidth));
            int nextPreviewWidth = currentPreviewWidth + deltaPixels;
            nextPreviewWidth = juce::jlimit(kMinPreviewWidth, splitWidth - kMinKeyboardWidth, nextPreviewWidth);

            previewPanelRatio = static_cast<float>(nextPreviewWidth) / static_cast<float>(splitWidth);
            resized();
        };
        previewKeyboardSplitter.onDragEnded = [this]
        {
            persistLayoutSettings();
        };

        browserPanel.onRootSelected = [this](std::optional<int64_t> rootId)
        {
            selectedRootFilterId = rootId;
            updateToolbarScanState(scanInProgress);
            refreshResults(currentSearchQuery);
        };

        browserPanel.onDeleteSelectedRootRequested = [this]()
        {
            handleDeleteRootClicked();
        };

        addRootToolbarButton.setImages(createFolderIcon(juce::Colours::white).release(),
                                       createFolderIcon(juce::Colours::lightgrey).release());
        addRootToolbarButton.setTooltip("Add a source folder to the library");
        addRootToolbarButton.onClick = [this]
        {
            handleAddRootClicked();
        };

        openSourceInExplorerToolbarButton.setImages(createExplorerIcon(juce::Colours::white).release(),
                                                    createExplorerIcon(juce::Colours::lightgrey).release());
        openSourceInExplorerToolbarButton.setTooltip("Open selected source in Windows File Explorer");
        openSourceInExplorerToolbarButton.onClick = [this]
        {
            handleOpenSourceInExplorerClicked();
        };

        deleteRootToolbarButton.setImages(createDeleteIcon(juce::Colours::white).release(),
                                          createDeleteIcon(juce::Colours::lightgrey).release());
        deleteRootToolbarButton.setTooltip("Delete selected source and its indexed files");
        deleteRootToolbarButton.onClick = [this]
        {
            handleDeleteRootClicked();
        };

        rescanToolbarButton.setImages(createRescanIcon(juce::Colours::white).release(),
                                      createRescanIcon(juce::Colours::lightgrey).release());
        rescanToolbarButton.setTooltip("Rescan selected source");
        rescanToolbarButton.onClick = [this]
        {
            handleRescanSelectedClicked();
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

        vacuumDbToolbarButton.setImages(createVacuumIcon(juce::Colours::white).release(),
                                        createVacuumIcon(juce::Colours::lightgrey).release());
        vacuumDbToolbarButton.setTooltip("Compress database (VACUUM)");
        vacuumDbToolbarButton.onClick = [this]
        {
            handleVacuumDatabaseClicked();
        };

        themeToolbarButton.onClick = [this]
        {
            applyThemeMode(!darkModeEnabled, true);
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
            restoreThemeSettings();
        }

        applyThemeMode(darkModeEnabled, false);

        resultsPanel.onSearchQueryChanged = [this](const std::string &query)
        {
            refreshResults(query);
        };

        resultsPanel.onFileSelected = [this](const FileRecord &file)
        {
            handleFileSelected(file, previewPanel.isAutoPlayEnabled(), false);
        };

        resultsPanel.onFileActivated = [this](const FileRecord &file)
        {
            handleFileSelected(file, true, true);
        };

        previewPanel.onPlayRequested = [this]
        {
            audioEngine.play();
            waveformPanel.setPlayheadNormalized(static_cast<float>(audioEngine.getPreviewPlaybackProgressNormalized()));
        };

        previewPanel.onStopRequested = [this]
        {
            audioEngine.stop();
            waveformPanel.setPlayheadNormalized(static_cast<float>(audioEngine.getPreviewPlaybackProgressNormalized()));
        };

        waveformPanel.onScrubRequested = [this](float normalizedPosition)
        {
            audioEngine.setPreviewPlaybackProgressNormalized(static_cast<double>(normalizedPosition));
            waveformPanel.setPlayheadNormalized(normalizedPosition);
        };

        previewPanel.onLoopPlaybackChanged = [this](bool enabled)
        {
            persistPreviewLoopEnabled(enabled);
            applyEffectiveLoopPlaybackMode();
        };

        previewPanel.onAutoPlaybackChanged = [this](bool enabled)
        {
            persistPreviewAutoPlayEnabled(enabled);
            applyEffectiveLoopPlaybackMode();
            repaint(0, getHeight() - kStatusBarHeight, getWidth(), kStatusBarHeight);
        };

        previewPanel.onPitchChanged = [this](double semitones)
        {
            audioEngine.setPitchSemitones(semitones);
            persistPreviewPitch(semitones);
        };

        previewPanel.onStretchChanged = [this](bool enabled)
        {
            audioEngine.setPreserveLengthEnabled(enabled);
            persistPreviewStretchEnabled(enabled);
        };

        previewPanel.onStretchHighQualityChanged = [this](bool enabled)
        {
            audioEngine.setStretchHighQualityEnabled(enabled);
            persistPreviewStretchHighQualityEnabled(enabled);
        };

        previewPanel.onOutputDeviceTypeChanged = [this](const juce::String &typeName)
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

        previewPanel.onOutputDeviceChanged = [this](const juce::String &deviceName)
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
        previewPanel.setStretchHighQualityAvailable(audioEngine.isStretchHighQualityAvailable());
        restoreAudioDeviceSettings();
        restorePreviewSettings();
        restoreMidiInputSettings();
        refreshOutputDeviceTypeList();
        refreshOutputDeviceList();
        refreshMidiInputDeviceList(true);

        midiRouter.setMidiCallback([this](const juce::MidiMessage &message)
                                   {
            if (message.isNoteOn())
            {
                juce::Component::SafePointer<MainComponent> safeThis(this);
                juce::MessageManager::callAsync([safeThis]
                                                {
                    if (safeThis == nullptr)
                        return;

                    bool changed = false;
                    if (safeThis->previewPanel.isAutoPlayEnabled())
                    {
                        safeThis->previewPanel.setAutoPlayEnabled(false);
                        safeThis->persistPreviewAutoPlayEnabled(false);
                        changed = true;
                    }

                    if (changed)
                    {
                        safeThis->applyEffectiveLoopPlaybackMode();
                        safeThis->repaint(0, safeThis->getHeight() - kStatusBarHeight, safeThis->getWidth(), kStatusBarHeight);
                    }
                });
            }

            audioEngine.handleMidiMessage(message); });
        midiRouter.attachKeyboardState(keyboardState);

        refreshMidiInputDeviceList(true);

        refreshRoots();
        refreshResults();
        restoreScanSummaryStatus();
        updateToolbarScanState(false);
        startTimerHz(kUiTimerHz);

        setSize(1200, 800);
    }

    MainComponent::~MainComponent()
    {
        tooltipWindow.setLookAndFeel(nullptr);
    }

    bool MainComponent::keyPressed(const juce::KeyPress &key)
    {
        if (key == juce::KeyPress::spaceKey)
        {
            if (audioEngine.isPreviewPlaying())
                audioEngine.stop();
            else
                audioEngine.play();

            waveformPanel.setPlayheadNormalized(static_cast<float>(audioEngine.getPreviewPlaybackProgressNormalized()));
            previewPanel.setPlaybackActive(audioEngine.isPreviewPlaying());
            return true;
        }

        return false;
    }

    void MainComponent::paint(juce::Graphics &g)
    {
        auto contentBounds = getLocalBounds();
        auto toolbarBounds = contentBounds.removeFromTop(kToolbarHeight);
        auto statusBarBounds = contentBounds.removeFromBottom(kStatusBarHeight);

        // Fill the entire content area so bare regions (keyboard pane) get
        // the correct background instead of the default dark LookAndFeel colour.
        g.setColour(darkModeEnabled ? juce::Colour(0xff333333) : juce::Colour(0xfff4f4f4));
        g.fillRect(contentBounds);

        g.setColour(darkModeEnabled ? juce::Colour(0xff272c33) : juce::Colour(0xffe8eaee));
        g.fillRect(toolbarBounds);

        g.setColour(darkModeEnabled ? juce::Colour(0xff1f242b) : juce::Colour(0xffedf1f5));
        g.fillRect(statusBarBounds);

        g.setColour(scanInProgress ? (darkModeEnabled ? juce::Colour(0xff8fe3a8) : juce::Colour(0xff1c6a3c))
                                   : (darkModeEnabled ? juce::Colour(0xff95a89b) : juce::Colour(0xff62756a)));
        g.setFont(16.0f);
        g.drawFittedText("Scan: " + scanStatusText,
                         statusBarBounds.reduced(10, 0),
                         juce::Justification::centredLeft,
                         1);

        const bool autoPlayEnabled = previewPanel.isAutoPlayEnabled();
        g.setColour(autoPlayEnabled ? (darkModeEnabled ? juce::Colour(0xff8cc8ff) : juce::Colour(0xff1f5f9b))
                                    : (darkModeEnabled ? juce::Colour(0xff9aa3ad) : juce::Colour(0xff5f6974)));
        g.drawFittedText("Auto: " + juce::String(previewPanel.isAutoPlayEnabled() ? "On" : "Off"),
                         statusBarBounds.reduced(10, 0),
                         juce::Justification::centredRight,
                         1);

        auto toolbarRightArea = toolbarBounds.reduced(12, 0).removeFromRight(240);
        g.setColour(darkModeEnabled ? juce::Colour(0xffbfe4ff) : juce::Colour(0xff1f4f7a));
        g.setFont(playbackTimeFont);
        g.drawText(playbackPositionText,
                   toolbarRightArea,
                   juce::Justification::centredRight,
                   false);

        if (toolbarFeedbackTicksRemaining > 0 && toolbarFeedbackText.isNotEmpty())
        {
            g.setColour(darkModeEnabled ? juce::Colours::lightgreen : juce::Colour(0xff1f7a43));
            g.setFont(12.0f);
            g.drawFittedText(toolbarFeedbackText,
                             toolbarBounds.reduced(10, 0).withTrimmedRight(250),
                             juce::Justification::centredRight,
                             1);
        }

        g.setColour(darkModeEnabled ? juce::Colours::darkgrey.withAlpha(0.9f) : juce::Colour(0xffbcbcbc));
        g.drawHorizontalLine(toolbarBounds.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
        g.drawHorizontalLine(statusBarBounds.getY(), 0.0f, static_cast<float>(getWidth()));
    }

    MainComponent::SplitterBar::SplitterBar(Orientation orientationIn)
        : orientation(orientationIn)
    {
        setMouseCursor(orientation == Orientation::vertical
                           ? juce::MouseCursor::LeftRightResizeCursor
                           : juce::MouseCursor::UpDownResizeCursor);
    }

    void MainComponent::SplitterBar::setDarkMode(bool enabled)
    {
        if (darkModeEnabled == enabled)
            return;

        darkModeEnabled = enabled;
        repaint();
    }

    void MainComponent::SplitterBar::paint(juce::Graphics &g)
    {
        g.fillAll(darkModeEnabled ? juce::Colours::darkgrey.withAlpha(0.75f)
                                  : juce::Colour(0xffd5d9df));
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

    juce::Rectangle<int> MainComponent::TooltipLookAndFeel::getTooltipBounds(const juce::String &tipText,
                                                                             juce::Point<int> screenPos,
                                                                             juce::Rectangle<int> parentArea)
    {
        auto area = parentArea;
        if (area.isEmpty())
        {
            const auto &displays = juce::Desktop::getInstance().getDisplays();

            if (const auto *display = displays.getDisplayForPoint(screenPos))
                area = display->userArea;
            else if (const auto *primaryDisplay = displays.getPrimaryDisplay())
                area = primaryDisplay->userArea;
            else
                area = juce::Rectangle<int>(0, 0, 1920, 1080);
        }

        const juce::Font font(juce::FontOptions(15.0f).withStyle("Bold"));
        juce::StringArray lines;
        lines.addLines(tipText);
        if (lines.isEmpty())
            lines.add(tipText);

        int maxLineWidth = 0;
        for (const auto &line : lines)
        {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText(font, line, 0.0f, 0.0f);
            const int lineWidth = static_cast<int>(std::ceil(glyphs.getBoundingBox(0, glyphs.getNumGlyphs(), true).getWidth()));
            maxLineWidth = juce::jmax(maxLineWidth, lineWidth);
        }

        constexpr int horizontalPadding = 14;
        constexpr int verticalPadding = 10;
        constexpr int lineGap = 2;

        const int lineHeight = static_cast<int>(std::ceil(font.getHeight()));
        const int lineCount = juce::jmax(1, lines.size());
        const int desiredWidth = maxLineWidth + horizontalPadding * 2;
        const int desiredHeight = (lineCount * lineHeight) + ((lineCount - 1) * lineGap) + verticalPadding * 2;

        const int maxAllowedWidth = juce::jmax(200, area.getWidth() - 20);
        const int finalWidth = juce::jlimit(200, maxAllowedWidth, desiredWidth);
        const int finalHeight = juce::jlimit(32, area.getHeight(), desiredHeight);

        juce::Rectangle<int> bounds(screenPos.x + 24, screenPos.y + 20, finalWidth, finalHeight);

        if (bounds.getRight() > area.getRight())
            bounds.setX(screenPos.x - finalWidth - 16);
        if (bounds.getBottom() > area.getBottom())
            bounds.setY(screenPos.y - finalHeight - 16);

        return bounds.constrainedWithin(area.reduced(2));
    }

    void MainComponent::TooltipLookAndFeel::drawTooltip(juce::Graphics &g,
                                                        const juce::String &text,
                                                        int width,
                                                        int height)
    {
        const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f);
        const auto background = darkModeEnabled ? juce::Colour(0xff1f2933) : juce::Colour(0xfff7fbff);
        const auto border = darkModeEnabled ? juce::Colour(0xff4b6378) : juce::Colour(0xffb7c8da);
        const auto textColour = darkModeEnabled ? juce::Colour(0xffe9f2fb) : juce::Colour(0xff22313f);

        g.setColour(background);
        g.fillRoundedRectangle(bounds, 7.0f);

        g.setColour(border);
        g.drawRoundedRectangle(bounds, 7.0f, 1.2f);

        juce::StringArray lines;
        lines.addLines(text);
        if (lines.isEmpty())
            lines.add(text);

        const juce::Font font(juce::FontOptions(15.0f).withStyle("Bold"));
        g.setFont(font);
        g.setColour(textColour);

        constexpr int horizontalPadding = 14;
        constexpr int verticalPadding = 10;
        constexpr int lineGap = 2;

        int y = verticalPadding;
        const int lineHeight = static_cast<int>(std::ceil(font.getHeight()));
        for (const auto &line : lines)
        {
            g.drawText(line,
                       horizontalPadding,
                       y,
                       width - horizontalPadding * 2,
                       lineHeight,
                       juce::Justification::centredLeft,
                       false);
            y += lineHeight + lineGap;
        }
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
        updateToolbarScanState(scanInProgress);
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
        else if (midiRouter.getActiveDeviceIdentifier() != selectedMidiInputIdentifier)
        {
            applyMidiInputSelection(selectedMidiInputIdentifier, false);
        }

        // Update combo box
        midiInputCombo.clear(juce::dontSendNotification);
        midiInputDeviceIdentifiers.clear();

        int id = 1;
        int selectedIndex = -1;
        for (const auto &device : devices)
        {
            midiInputCombo.addItem(device.name, id++);
            midiInputDeviceIdentifiers.add(device.identifier);

            if (device.identifier == selectedMidiInputIdentifier)
                selectedIndex = midiInputDeviceIdentifiers.size() - 1;
        }

        if (selectedIndex >= 0)
            midiInputCombo.setSelectedItemIndex(selectedIndex, juce::dontSendNotification);
        else if (devices.isEmpty())
            midiInputCombo.setText("", juce::dontSendNotification);
        else
            midiInputCombo.setSelectedItemIndex(0, juce::dontSendNotification);
    }

    void MainComponent::applyMidiInputSelection(const juce::String &deviceIdentifier, bool persistSelection)
    {
        selectedMidiInputIdentifier = deviceIdentifier;

        midiRouter.disableAllDevices();
        if (selectedMidiInputIdentifier.isNotEmpty())
            midiRouter.enableDevice(selectedMidiInputIdentifier);

        if (persistSelection)
            persistMidiInputSelection(selectedMidiInputIdentifier);

        // Update combo box to reflect current selection
        const auto devices = juce::MidiInput::getAvailableDevices();
        midiInputCombo.clear(juce::dontSendNotification);
        midiInputDeviceIdentifiers.clear();

        int id = 1;
        int selectedIndex = -1;
        for (const auto &device : devices)
        {
            midiInputCombo.addItem(device.name, id++);
            midiInputDeviceIdentifiers.add(device.identifier);

            if (device.identifier == selectedMidiInputIdentifier)
                selectedIndex = midiInputDeviceIdentifiers.size() - 1;
        }

        if (selectedIndex >= 0)
            midiInputCombo.setSelectedItemIndex(selectedIndex, juce::dontSendNotification);
        else if (devices.isEmpty())
            midiInputCombo.setText("", juce::dontSendNotification);
        else
            midiInputCombo.setSelectedItemIndex(0, juce::dontSendNotification);
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
        if (savedPitch.has_value())
        {
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

        bool loopEnabled = false;
        if (const auto savedLoop = catalogDb.getAppSetting("preview.loopEnabled"))
            loopEnabled = (*savedLoop == "1" || *savedLoop == "true" || *savedLoop == "True");

        bool autoPlayEnabled = false;
        if (const auto savedAuto = catalogDb.getAppSetting("preview.autoPlayEnabled"))
            autoPlayEnabled = (*savedAuto == "1" || *savedAuto == "true" || *savedAuto == "True");

        bool stretchEnabled = false;
        if (const auto savedStretch = catalogDb.getAppSetting("preview.stretchEnabled"))
            stretchEnabled = (*savedStretch == "1" || *savedStretch == "true" || *savedStretch == "True");
        else if (const auto savedPreserveLength = catalogDb.getAppSetting("preview.preserveLengthEnabled"))
            stretchEnabled = (*savedPreserveLength == "1" || *savedPreserveLength == "true" || *savedPreserveLength == "True");

        bool stretchHighQualityEnabled = false;
        if (const auto savedStretchHq = catalogDb.getAppSetting("preview.stretchHighQualityEnabled"))
            stretchHighQualityEnabled = (*savedStretchHq == "1" || *savedStretchHq == "true" || *savedStretchHq == "True");

        if (!audioEngine.isStretchHighQualityAvailable())
            stretchHighQualityEnabled = false;

        previewPanel.setAutoPlayEnabled(autoPlayEnabled);
        previewPanel.setLoopEnabled(loopEnabled);
        previewPanel.setStretchEnabled(stretchEnabled);
        previewPanel.setStretchHighQualityEnabled(stretchHighQualityEnabled);
        audioEngine.setPreserveLengthEnabled(stretchEnabled);
        audioEngine.setStretchHighQualityEnabled(stretchHighQualityEnabled);
        applyEffectiveLoopPlaybackMode();
    }

    void MainComponent::restoreMidiInputSettings()
    {
        if (const auto saved = catalogDb.getAppSetting("midi.inputDeviceIdentifier"))
            selectedMidiInputIdentifier = *saved;
    }

    void MainComponent::restoreThemeSettings()
    {
        if (const auto savedThemeMode = catalogDb.getAppSetting("ui.themeMode"))
            darkModeEnabled = (*savedThemeMode != "light");
    }

    void MainComponent::persistPreviewPitch(double semitones)
    {
        catalogDb.setAppSetting("preview.pitchSemitones", juce::String(semitones).toStdString());
    }

    void MainComponent::persistPreviewAutoPlayEnabled(bool enabled)
    {
        catalogDb.setAppSetting("preview.autoPlayEnabled", enabled ? "1" : "0");
    }

    void MainComponent::persistPreviewLoopEnabled(bool enabled)
    {
        catalogDb.setAppSetting("preview.loopEnabled", enabled ? "1" : "0");
    }

    void MainComponent::persistPreviewStretchEnabled(bool enabled)
    {
        catalogDb.setAppSetting("preview.stretchEnabled", enabled ? "1" : "0");
        catalogDb.setAppSetting("preview.preserveLengthEnabled", enabled ? "1" : "0");
    }

    void MainComponent::persistPreviewStretchHighQualityEnabled(bool enabled)
    {
        catalogDb.setAppSetting("preview.stretchHighQualityEnabled", enabled ? "1" : "0");
    }

    void MainComponent::persistThemeMode(bool darkMode)
    {
        catalogDb.setAppSetting("ui.themeMode", darkMode ? "dark" : "light");
    }

    void MainComponent::applyThemeMode(bool darkMode, bool persist)
    {
        darkModeEnabled = darkMode;

        const auto normalIconColour = darkModeEnabled ? juce::Colours::white : juce::Colour(0xff202020);
        const auto hoverIconColour = darkModeEnabled ? juce::Colours::lightgrey : juce::Colour(0xff4a4a4a);

        browserPanel.setDarkMode(darkModeEnabled);
        resultsPanel.setDarkMode(darkModeEnabled);
        previewPanel.setDarkMode(darkModeEnabled);
        waveformPanel.setDarkMode(darkModeEnabled);
        leftRightSplitter.setDarkMode(darkModeEnabled);
        resultsWaveformSplitter.setDarkMode(darkModeEnabled);
        waveformBottomSplitter.setDarkMode(darkModeEnabled);
        previewKeyboardSplitter.setDarkMode(darkModeEnabled);

        // Apply dark mode colors to keyboard
        if (darkModeEnabled)
        {
            keyboard.setColour(juce::MidiKeyboardComponent::ColourIds::whiteNoteColourId, juce::Colour(0xff34383c));
            keyboard.setColour(juce::MidiKeyboardComponent::ColourIds::blackNoteColourId, juce::Colour(0xff1c1e20));
            keyboard.setColour(juce::MidiKeyboardComponent::ColourIds::mouseOverKeyOverlayColourId, juce::Colour(0x4fffffff));
            keyboard.setColour(juce::MidiKeyboardComponent::ColourIds::keyDownOverlayColourId, juce::Colour(0xcc3f7fb5));
        }
        else
        {
            keyboard.setColour(juce::MidiKeyboardComponent::ColourIds::whiteNoteColourId, juce::Colours::white);
            keyboard.setColour(juce::MidiKeyboardComponent::ColourIds::blackNoteColourId, juce::Colours::black);
            keyboard.setColour(juce::MidiKeyboardComponent::ColourIds::mouseOverKeyOverlayColourId, juce::Colour(0x4f000000));
            keyboard.setColour(juce::MidiKeyboardComponent::ColourIds::keyDownOverlayColourId, juce::Colour(0xcc3f7fb5));
        }

        // Apply dark mode colors to MIDI input combo (match audio device combo styling)
        const auto controlBg = darkModeEnabled ? juce::Colour(0xff2b2b2b) : juce::Colour(0xffffffff);
        const auto textColour = darkModeEnabled ? juce::Colours::white : juce::Colour(0xff202020);
        const auto outline = darkModeEnabled ? juce::Colour(0xff4d4d4d) : juce::Colour(0xffb8b8b8);
        const auto arrowColour = darkModeEnabled ? juce::Colour(0xffcccccc) : juce::Colour(0xff606060);
        midiInputCombo.setColour(juce::ComboBox::backgroundColourId, controlBg);
        midiInputCombo.setColour(juce::ComboBox::textColourId, textColour);
        midiInputCombo.setColour(juce::ComboBox::outlineColourId, outline);
        midiInputCombo.setColour(juce::ComboBox::arrowColourId, arrowColour);

        addRootToolbarButton.setImages(createFolderIcon(normalIconColour).release(),
                                       createFolderIcon(hoverIconColour).release());
        openSourceInExplorerToolbarButton.setImages(createExplorerIcon(normalIconColour).release(),
                                                    createExplorerIcon(hoverIconColour).release());
        deleteRootToolbarButton.setImages(createDeleteIcon(normalIconColour).release(),
                                          createDeleteIcon(hoverIconColour).release());
        rescanToolbarButton.setImages(createRescanIcon(normalIconColour).release(),
                                      createRescanIcon(hoverIconColour).release());
        cancelScanToolbarButton.setImages(createCancelIcon(normalIconColour).release(),
                                          createCancelIcon(hoverIconColour).release());
        resetLayoutToolbarButton.setImages(createResetLayoutIcon(normalIconColour).release(),
                                           createResetLayoutIcon(hoverIconColour).release());
        vacuumDbToolbarButton.setImages(createVacuumIcon(normalIconColour).release(),
                                        createVacuumIcon(hoverIconColour).release());

        themeToolbarButton.setImages(
            createThemeIcon(normalIconColour, darkModeEnabled).release(),
            createThemeIcon(hoverIconColour, darkModeEnabled).release());
        themeToolbarButton.setTooltip(darkModeEnabled ? "Switch to Light Mode" : "Switch to Dark Mode");
        tooltipLookAndFeel.setDarkMode(darkModeEnabled);
        tooltipWindow.repaint();

        if (persist)
            persistThemeMode(darkModeEnabled);

        repaint();
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

        if (const auto value = catalogDb.getAppSetting("layout.waveformPanelRatio"))
        {
            try
            {
                waveformPanelRatio = juce::jlimit(0.30f, 0.70f, std::stof(*value));
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
        catalogDb.setAppSetting("layout.waveformPanelRatio", juce::String(waveformPanelRatio, 4).toStdString());
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
            setScanStatusText(*status);
            return;
        }

        setScanStatusText("Idle");
    }

    void MainComponent::persistScanSummaryStatus(const juce::String &statusText)
    {
        catalogDb.setAppSetting("scan.lastSummaryStatus", statusText.toStdString());
    }

    void MainComponent::setScanStatusText(const juce::String &statusText)
    {
        if (scanStatusText == statusText)
            return;

        scanStatusText = statusText;
        repaint(0, getHeight() - kStatusBarHeight, getWidth(), kStatusBarHeight);
    }

    void MainComponent::updateWindowTitleForLoadedFile(const juce::String &fullPath)
    {
        if (auto *window = findParentComponentOfClass<juce::DocumentWindow>())
            window->setName("Sample Wrangler - [" + fullPath + "]");
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
        currentSelectedFile = file;
        const uint64_t requestId = previewLoadRequestCounter.fetch_add(1, std::memory_order_acq_rel) + 1;

        const bool hasLoopMetadata = hasEmbeddedLoopMetadata(file);
        const bool hasValidEmbeddedLoopRegion = hasLoopMetadata && file.loopStartSample.has_value() && file.loopEndSample.has_value() &&
                                                *file.loopEndSample > *file.loopStartSample;
        const bool hasSampleLength = file.totalSamples.has_value() && *file.totalSamples > 1;

        audioEngine.setPreviewRootMidiNote(hasLoopMetadata && file.acidRootNote.has_value() ? *file.acidRootNote : 60);

        if (hasLoopMetadata && hasValidEmbeddedLoopRegion)
        {
            audioEngine.setPreviewLoopRegionSamples(*file.loopStartSample, *file.loopEndSample);

            if (!previewPanel.isLoopEnabled() && !previewPanel.isAutoPlayEnabled())
            {
                previewPanel.setLoopEnabled(true);
            }
        }
        else if (hasLoopMetadata && hasSampleLength)
        {
            audioEngine.setPreviewLoopRegionSamples(0, *file.totalSamples - 1);

            if (!previewPanel.isLoopEnabled() && !previewPanel.isAutoPlayEnabled())
            {
                previewPanel.setLoopEnabled(true);
            }
        }
        else
        {
            audioEngine.clearPreviewLoopRegion();
        }

        applyEffectiveLoopPlaybackMode();
        updateWaveformLoopOverlay();

        if (file.indexOnly)
        {
            waveformPanel.setPeaks({});
            waveformPanel.setPlayheadNormalized(-1.0f);
            waveformPanel.setLoopRegionNormalized(-1.0f, -1.0f);
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
            [safeThis, absolutePath, playWhenReady, requestId](const std::atomic<uint64_t> &cancelGeneration, uint64_t jobGeneration)
            {
                if (cancelGeneration.load(std::memory_order_relaxed) != jobGeneration)
                    return;

                if (safeThis == nullptr)
                    return;

                if (safeThis->previewLoadRequestCounter.load(std::memory_order_acquire) != requestId)
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

                auto peaks = std::make_shared<std::vector<std::vector<float>>>();
                peaks->resize(static_cast<size_t>(numChannels));
                for (auto &channelPeaks : *peaks)
                    channelPeaks.resize(static_cast<size_t>(peakCount), 0.0f);

                for (int p = 0; p < peakCount; ++p)
                {
                    const int start = p * samplesPerPeak;
                    const int end = std::min(numSamples, start + samplesPerPeak);

                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        float maxAbs = 0.0f;
                        const auto *channelData = tempBuffer.getReadPointer(ch);
                        for (int s = start; s < end; ++s)
                            maxAbs = std::max(maxAbs, std::abs(channelData[s]));

                        (*peaks)[static_cast<size_t>(ch)][static_cast<size_t>(p)] = maxAbs;
                    }
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

                juce::MessageManager::callAsync([safeThis, interleaved, peaks, numChannels, numSamples, sampleRate, playWhenReady, absolutePath, requestId]
                                                {
                    if (safeThis == nullptr)
                        return;

                    if (safeThis->previewLoadRequestCounter.load(std::memory_order_acquire) != requestId)
                        return;

                    auto previewBuffer = std::make_unique<juce::AudioBuffer<float>>(numChannels, numSamples);
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        auto *dst = previewBuffer->getWritePointer(ch);
                        for (int s = 0; s < numSamples; ++s)
                            dst[s] = (*interleaved)[static_cast<size_t>(ch * numSamples + s)];
                    }

                    safeThis->audioEngine.loadPreviewBuffer(std::move(previewBuffer), sampleRate);
                    safeThis->updateWindowTitleForLoadedFile(absolutePath);
                    safeThis->waveformPanel.setPeaks(*peaks);
                    safeThis->waveformPanel.setPlayheadNormalized(0.0f);
                    safeThis->updateWaveformLoopOverlay();
                    if (playWhenReady)
                        safeThis->audioEngine.play(); });
            },
            JobPriority::High});
    }

    void MainComponent::updateWaveformLoopOverlay()
    {
        if (!currentSelectedFile.has_value() || currentSelectedFile->indexOnly)
        {
            waveformPanel.setLoopRegionNormalized(-1.0f, -1.0f);
            return;
        }

        const auto &file = *currentSelectedFile;
        if (hasEmbeddedLoopMetadata(file) &&
            file.totalSamples.has_value() && *file.totalSamples > 1 &&
            file.loopStartSample.has_value() && file.loopEndSample.has_value() &&
            *file.loopEndSample > *file.loopStartSample)
        {
            const double length = static_cast<double>(*file.totalSamples);
            const float loopStartNorm = static_cast<float>(juce::jlimit(0.0, 1.0, static_cast<double>(*file.loopStartSample) / length));
            const float loopEndNorm = static_cast<float>(juce::jlimit(0.0, 1.0, static_cast<double>(*file.loopEndSample) / length));

            if (loopEndNorm > loopStartNorm)
            {
                waveformPanel.setLoopRegionNormalized(loopStartNorm, loopEndNorm);
                return;
            }
        }

        if (hasEmbeddedLoopMetadata(file) &&
            file.totalSamples.has_value() && *file.totalSamples > 1)
        {
            waveformPanel.setLoopRegionNormalized(0.0f, 1.0f);
            return;
        }

        if (previewPanel.isLoopEnabled())
            waveformPanel.setLoopRegionNormalized(0.0f, 1.0f);
        else
            waveformPanel.setLoopRegionNormalized(-1.0f, -1.0f);
    }

    void MainComponent::timerCallback()
    {
        ++midiDeviceRefreshCounter;
        if (midiDeviceRefreshCounter >= kMidiDeviceRefreshIntervalTicks)
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

        const float progress = static_cast<float>(audioEngine.getPreviewPlaybackProgressNormalized());
        waveformPanel.setPlayheadNormalized(progress);
        previewPanel.setPlaybackActive(audioEngine.isPreviewPlaying());

        double durationSeconds = 0.0;
        if (currentSelectedFile.has_value() && !currentSelectedFile->indexOnly)
        {
            if (currentSelectedFile->durationSec.has_value() && *currentSelectedFile->durationSec > 0.0)
            {
                durationSeconds = *currentSelectedFile->durationSec;
            }
            else if (currentSelectedFile->totalSamples.has_value() && currentSelectedFile->sampleRate.has_value() && *currentSelectedFile->sampleRate > 0)
            {
                durationSeconds = static_cast<double>(*currentSelectedFile->totalSamples) / static_cast<double>(*currentSelectedFile->sampleRate);
            }
        }

        const juce::String nextPlaybackPositionText = formatClockHmsMs(durationSeconds * static_cast<double>(progress));
        if (playbackPositionText != nextPlaybackPositionText)
        {
            playbackPositionText = nextPlaybackPositionText;
            repaint(0, 0, getWidth(), kToolbarHeight);
        }

        if (previewPanel.isAutoPlayEnabled() && audioEngine.consumePreviewFinishedFlag())
            advanceAutoplaySelectionAndPlay();
    }

    void MainComponent::applyEffectiveLoopPlaybackMode()
    {
        const bool loopCurrentFile = previewPanel.isLoopEnabled() && !previewPanel.isAutoPlayEnabled();
        audioEngine.setLoopEnabled(loopCurrentFile);
        updateWaveformLoopOverlay();
    }

    void MainComponent::advanceAutoplaySelectionAndPlay()
    {
        const int rowCount = resultsPanel.getResultCount();
        if (rowCount <= 0)
            return;

        int candidateRow = resultsPanel.getSelectedRow();
        if (candidateRow < 0)
            candidateRow = -1;

        const bool wrapList = previewPanel.isLoopEnabled();
        const int maxSteps = wrapList ? rowCount : (rowCount - candidateRow - 1);

        for (int step = 0; step < maxSteps; ++step)
        {
            int nextRow = candidateRow + 1 + step;
            if (wrapList)
                nextRow %= rowCount;
            else if (nextRow >= rowCount)
                return;

            const auto *nextFile = resultsPanel.getResultAt(nextRow);
            if (nextFile == nullptr || nextFile->indexOnly)
                continue;

            resultsPanel.selectRow(nextRow);
            return;
        }
    }

    void MainComponent::handleAddRootClicked()
    {
        juce::File initialDirectory;
        if (const auto savedRootPath = catalogDb.getAppSetting("roots.lastAddedPath"))
            initialDirectory = juce::File(*savedRootPath);

        rootChooser = std::make_unique<juce::FileChooser>("Select a source folder", initialDirectory);

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
                                                       "Add Source Failed",
                                                       "Could not add source:\n" + selected.getFullPathName());
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
        setScanStatusText("Scanning [" + rootDisplayName + "]... 0 files (0.0s)");

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
                    setScanStatusText("Scanning [" + rootDisplayName + "]... " + juce::String(scannedFilesCount)
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
                    const auto summary = "Idle (last [" + rootDisplayName + "]: " + juce::String(scannedFilesCount)
                                       + " files in " + juce::String(elapsedSec, 1) + "s)";
                    setScanStatusText(summary);
                    persistScanSummaryStatus(summary);
                    updateToolbarScanState(false);
                    refreshResults();
                    resultsPanel.selectFirstRowIfNoneSelected();

                    if (onCompleted)
                        onCompleted(); });
            });
    }

    void MainComponent::handleRescanSelectedClicked()
    {
        if (scanInProgress)
            return;

        if (!selectedRootFilterId.has_value())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                   "Rescan Selected Source",
                                                   "Select a source in the Browser panel to rescan.");
            return;
        }

        const auto roots = catalogDb.allRoots();
        const auto it = std::find_if(roots.begin(), roots.end(), [this](const RootRecord &root)
                                     { return root.id == *selectedRootFilterId; });
        if (it == roots.end())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Rescan Selected Source",
                                                   "The selected source is no longer available.");
            refreshRoots();
            updateToolbarScanState(scanInProgress);
            return;
        }

        startRootScan(it->id, it->path, juce::String(it->label));
    }

    void MainComponent::cancelScan()
    {
        if (!scanInProgress)
            return;

        const auto now = std::chrono::steady_clock::now();
        const auto elapsedSec = std::chrono::duration<double>(now - scanStartTime).count();
        setScanStatusText("Cancelling... (" + juce::String(elapsedSec, 1) + "s)");

        const int cancelledCount = scannedFilesCount;
        const double cancelledElapsedSec = elapsedSec;
        jobQueue.cancelAll();

        juce::MessageManager::callAsync([this, cancelledCount, cancelledElapsedSec]
                                        {
            scanInProgress = false;
            const auto summary = "Idle (cancelled: " + juce::String(cancelledCount)
                               + " files in " + juce::String(cancelledElapsedSec, 1) + "s)";
            setScanStatusText(summary);
            persistScanSummaryStatus(summary);
            updateToolbarScanState(false);
            refreshResults();
            resultsPanel.selectFirstRowIfNoneSelected(); });
    }

    void MainComponent::handleDeleteRootClicked()
    {
        if (!selectedRootFilterId.has_value() || scanInProgress)
            return;

        const auto roots = catalogDb.allRoots();
        const auto it = std::find_if(roots.begin(), roots.end(), [this](const RootRecord &root)
                                     { return root.id == *selectedRootFilterId; });
        if (it == roots.end())
            return;

        const auto rootIdToDelete = it->id;
        const auto rootLabel = juce::String(it->label);
        juce::Component::SafePointer<MainComponent> safeThis(this);

        juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::WarningIcon,
            "Delete Source",
            "Delete source '" + rootLabel + "' and all indexed files under it?",
            "Delete",
            "Cancel",
            nullptr,
            juce::ModalCallbackFunction::create([safeThis, rootIdToDelete](int result)
                                                {
                if (safeThis == nullptr || result != 1)
                    return;

                if (!safeThis->catalogDb.removeRoot(rootIdToDelete))
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           "Delete Source Failed",
                                                           "Unable to delete selected source.");
                    return;
                }

                if (safeThis->selectedRootFilterId.has_value() && *safeThis->selectedRootFilterId == rootIdToDelete)
                    safeThis->selectedRootFilterId.reset();

                safeThis->refreshRoots();
                safeThis->refreshResults(safeThis->currentSearchQuery);

                safeThis->toolbarFeedbackText = "Source deleted";
                safeThis->toolbarFeedbackTicksRemaining = 60;
                safeThis->repaint(0, 0, safeThis->getWidth(), kToolbarHeight); }));
    }

    void MainComponent::handleOpenSourceInExplorerClicked()
    {
        if (!selectedRootFilterId.has_value())
            return;

        const auto rootPath = rootPathForId(*selectedRootFilterId);
        if (rootPath.empty())
            return;

        juce::File rootFolder(rootPath);
        if (!rootFolder.exists() || !rootFolder.isDirectory())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Source Missing",
                                                   "The selected source folder is not available:\n" + rootFolder.getFullPathName());
            return;
        }

        rootFolder.revealToUser();
    }

    void MainComponent::handleVacuumDatabaseClicked()
    {
        if (scanInProgress)
            return;

        if (!catalogDb.vacuum())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Database Compression Failed",
                                                   "Unable to compact the database.");
            return;
        }

        toolbarFeedbackText = "Database compressed";
        toolbarFeedbackTicksRemaining = 60;
        repaint(0, 0, getWidth(), kToolbarHeight);
    }

    void MainComponent::updateToolbarScanState(bool inProgress)
    {
        addRootToolbarButton.setEnabled(!inProgress);
        openSourceInExplorerToolbarButton.setEnabled(selectedRootFilterId.has_value());
        deleteRootToolbarButton.setEnabled(!inProgress && selectedRootFilterId.has_value());
        rescanToolbarButton.setEnabled(!inProgress && selectedRootFilterId.has_value());
        cancelScanToolbarButton.setEnabled(inProgress);
        vacuumDbToolbarButton.setEnabled(!inProgress && catalogDb.isOpen());
    }

    void MainComponent::resetLayout()
    {
        leftPanelRatio = kDefaultLeftPanelRatio;
        waveformPanelRatio = kDefaultWaveformPanelRatio;
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

        auto toolbarStrip = area.removeFromTop(kToolbarHeight);
        toolbar.setBounds(toolbarStrip);

        area.removeFromBottom(kStatusBarHeight);

        constexpr int iconButtonSize = 42;
        constexpr int toolbarGap = 4;

        auto toolbarArea = toolbarStrip.reduced(6, (kToolbarHeight - iconButtonSize) / 2);
        addRootToolbarButton.setBounds(toolbarArea.removeFromLeft(iconButtonSize));
        toolbarArea.removeFromLeft(toolbarGap);
        openSourceInExplorerToolbarButton.setBounds(toolbarArea.removeFromLeft(iconButtonSize));
        toolbarArea.removeFromLeft(toolbarGap);
        rescanToolbarButton.setBounds(toolbarArea.removeFromLeft(iconButtonSize));
        toolbarArea.removeFromLeft(toolbarGap);
        cancelScanToolbarButton.setBounds(toolbarArea.removeFromLeft(iconButtonSize));
        toolbarArea.removeFromLeft(toolbarGap);
        deleteRootToolbarButton.setBounds(toolbarArea.removeFromLeft(iconButtonSize));
        toolbarArea.removeFromLeft(toolbarGap);
        vacuumDbToolbarButton.setBounds(toolbarArea.removeFromLeft(iconButtonSize));
        toolbarArea.removeFromLeft(toolbarGap);
        resetLayoutToolbarButton.setBounds(toolbarArea.removeFromLeft(iconButtonSize));
        toolbarArea.removeFromLeft(toolbarGap);
        themeToolbarButton.setBounds(toolbarArea.removeFromLeft(iconButtonSize));

        const int totalWidth = area.getWidth() - kSplitterThickness;
        int leftWidth = static_cast<int>(leftPanelRatio * static_cast<float>(totalWidth));
        leftWidth = juce::jlimit(kMinLeftPanelWidth, juce::jmax(kMinLeftPanelWidth, totalWidth - kMinRightPanelWidth), leftWidth);

        browserPanel.setBounds(area.removeFromLeft(leftWidth));
        leftRightSplitter.setBounds(area.removeFromLeft(kSplitterThickness));

        auto rightArea = area;
        const int totalHeight = rightArea.getHeight() - kSplitterThickness * 2;
        int waveformHeight = static_cast<int>(waveformPanelRatio * static_cast<float>(totalHeight));
        waveformHeight = juce::jlimit(kMinWaveformHeight, juce::jmax(kMinWaveformHeight, totalHeight - kMinResultsHeight - kMinBottomHeight), waveformHeight);
        int bottomHeight = static_cast<int>(bottomPanelRatio * static_cast<float>(totalHeight));
        bottomHeight = juce::jlimit(kMinBottomHeight, juce::jmax(kMinBottomHeight, totalHeight - kMinResultsHeight - kMinWaveformHeight), bottomHeight);
        int resultsHeight = totalHeight - waveformHeight - bottomHeight;

        resultsPanel.setBounds(rightArea.removeFromTop(resultsHeight));
        resultsWaveformSplitter.setBounds(rightArea.removeFromTop(kSplitterThickness));
        waveformPanel.setBounds(rightArea.removeFromTop(waveformHeight));
        waveformBottomSplitter.setBounds(rightArea.removeFromTop(kSplitterThickness));

        auto bottomArea = rightArea;
        const int bottomWidth = bottomArea.getWidth() - kSplitterThickness;
        int previewWidth = static_cast<int>(previewPanelRatio * static_cast<float>(bottomWidth));
        previewWidth = juce::jlimit(kMinPreviewWidth, juce::jmax(kMinPreviewWidth, bottomWidth - kMinKeyboardWidth), previewWidth);

        previewPanel.setBounds(bottomArea.removeFromLeft(previewWidth));
        previewKeyboardSplitter.setBounds(bottomArea.removeFromLeft(kSplitterThickness));

        // Keyboard pane: MIDI selector on top, keyboard below
        auto keyboardArea = bottomArea;
        constexpr int midiComboHeight = 24;
        constexpr int midiComboMargin = 3;
        constexpr int midiKeyboardPadding = 6;
        auto midiComboArea = keyboardArea.removeFromTop(midiComboHeight + midiComboMargin * 2).reduced(midiComboMargin);
        midiInputCombo.setBounds(midiComboArea);
        keyboardArea.removeFromTop(midiKeyboardPadding);
        keyboard.setBounds(keyboardArea);
    }

} // namespace sw
