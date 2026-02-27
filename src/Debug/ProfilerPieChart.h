#ifndef PROFILERPIECHART_H
#define PROFILERPIECHART_H

#include "Rendering/Core/Graphics2D.h"
#include "Scene/Scene.h"
#include "Engine/Core/GameEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

class ProfilerPieChart {
    public:
        void setCaptureEnabled(bool enabled){
            if(captureEnabled == enabled){
                return;
            }
            captureEnabled = enabled;
            sampleAccumSec = enabled ? sampleIntervalSec : 0.0f; // force immediate refresh on enable
        }

        void updateFromSources(float deltaTime, const Scene::DebugStats& stats, const GameEngine* engine){
            if(!captureEnabled || !engine){
                return;
            }

            sampleAccumSec += std::max(0.0f, deltaTime);
            if(sampleAccumSec < sampleIntervalSec){
                return;
            }
            sampleAccumSec = 0.0f;

            Sample raw{};
            raw.shadowMs = clampNonNegative(stats.shadowMs.load(std::memory_order_relaxed));
            raw.drawMs = clampNonNegative(stats.drawMs.load(std::memory_order_relaxed));
            raw.postFxMs = clampNonNegative(stats.postFxMs.load(std::memory_order_relaxed));
            raw.renderMs = clampNonNegative(engine->getLastRenderMs());
            raw.renderSceneMs = clampNonNegative(engine->getLastRenderSceneMs());
            raw.renderBlitMs = clampNonNegative(engine->getLastRenderBlitMs());
            raw.renderImGuiMs = clampNonNegative(engine->getLastRenderImGuiMs());
            raw.renderWaitMs = clampNonNegative(engine->getLastRenderWaitMs());
            raw.swapMs = clampNonNegative(engine->getLastSwapMs());

            if(!hasSample){
                sample = raw;
                hasSample = true;
                return;
            }

            sample.shadowMs = blend(sample.shadowMs, raw.shadowMs);
            sample.drawMs = blend(sample.drawMs, raw.drawMs);
            sample.postFxMs = blend(sample.postFxMs, raw.postFxMs);
            sample.renderMs = blend(sample.renderMs, raw.renderMs);
            sample.renderSceneMs = blend(sample.renderSceneMs, raw.renderSceneMs);
            sample.renderBlitMs = blend(sample.renderBlitMs, raw.renderBlitMs);
            sample.renderImGuiMs = blend(sample.renderImGuiMs, raw.renderImGuiMs);
            sample.renderWaitMs = blend(sample.renderWaitMs, raw.renderWaitMs);
            sample.swapMs = blend(sample.swapMs, raw.swapMs);
        }

        void draw(Graphics2D& g, float x, float y, float w, float h, const GameEngine* engine = nullptr){
            if(!captureEnabled || !hasSample || w <= 0.0f || h <= 0.0f){
                return;
            }

            const float sceneMs = sample.renderSceneMs;
            const float blitMs = sample.renderBlitMs;
            const float imguiMs = sample.renderImGuiMs;
            const float renderWaitMs = sample.renderWaitMs;
            const float renderOtherMs = std::max(0.0f, sample.renderMs - (sceneMs + blitMs + imguiMs + renderWaitMs));
            const float presentMs = sample.swapMs;

            std::array<Slice, kSliceCount> slices = {
                Slice{"scene", sceneMs, Color::fromRGB24(0x56AEC1)},
                Slice{"blit", blitMs, Color::fromRGB24(0x4D74D5)},
                Slice{"imgui", imguiMs, Color::fromRGB24(0x65B96A)},
                Slice{"wait", renderWaitMs, Color::fromRGB24(0xD95454)},
                Slice{"other", renderOtherMs, Color::fromRGB24(0xB18045)},
                Slice{"swap", presentMs, Color::fromRGB24(0xD8D8D8)}
            };

            float totalMs = 0.0f;
            for(const auto& slice : slices){
                totalMs += slice.ms;
            }
            if(totalMs <= 0.0001f){
                return;
            }

            const float fontPx = static_cast<float>(std::max(10, g.getContext().fontSize.get()));
            const float textBaselineOffset = std::max(8.0f, fontPx - 2.0f);
            const float panelPad = 10.0f;
            const float headerY = y + panelPad + textBaselineOffset;
            const float headerBlockH = (fontPx * 2.0f) + 8.0f;
            const float contentY = y + panelPad + headerBlockH;
            const float contentH = std::max(40.0f, h - (panelPad * 2.0f) - headerBlockH);
            const float minLegendWidth = 120.0f;
            const float legendGap = 12.0f;
            const float maxPieByHeight = std::max(48.0f, contentH - 6.0f);
            const float maxPieByWidth = std::max(56.0f, w - (panelPad * 2.0f) - minLegendWidth - legendGap);
            const float pieDiameter = std::max(48.0f, std::min(maxPieByHeight, maxPieByWidth));
            const float pieRadius = pieDiameter * 0.5f;
            const float pieCenterX = x + panelPad + pieRadius;
            const float pieCenterY = contentY + (contentH * 0.5f);
            const float pieDepth = std::max(6.0f, pieRadius * 0.18f);
            const float legendStartX = x + panelPad + pieDiameter + legendGap;
            const float legendY = contentY;
            const float legendLineH = fontPx + 2.0f;

            Graphics2D::SetBackgroundColor(g, Color::fromRGBA255(10, 10, 16, 190));
            Graphics2D::FillRect(g, x, y, w, h);
            Graphics2D::SetBackgroundColor(g, Color::fromRGBA255(60, 70, 85, 230));
            Graphics2D::DrawRect(g, x, y, w, h);

            Graphics2D::SetForegroundColor(g, Color::WHITE);
            char titleLine[128];
            std::snprintf(
                titleLine,
                sizeof(titleLine),
                "[render_root] %.1f ms | %.0f FPS",
                totalMs,
                (totalMs > 0.0001f) ? (1000.0f / totalMs) : 0.0f
            );
            Graphics2D::DrawString(g, titleLine, x + panelPad, headerY, true);

            if(engine){
                char modeLine[128];
                const char* frameCapLabel = nullptr;
                char frameCapBuffer[32];
                if(engine->getFrameCap() == GameEngine::FrameCapUncapped){
                    frameCapLabel = "uncapped";
                }else{
                    std::snprintf(frameCapBuffer, sizeof(frameCapBuffer), "%d", engine->getFrameCap());
                    frameCapLabel = frameCapBuffer;
                }

                std::snprintf(
                    modeLine,
                    sizeof(modeLine),
                    "vsync=%s | frame_cap=%s",
                    vsyncModeToString(engine->getVSyncMode()),
                    frameCapLabel
                );
                Graphics2D::DrawString(g, modeLine, x + panelPad, headerY + fontPx, true);
            }

            std::array<float, kSliceCount> sweeps{};
            float currentAngle = -Math3D::PI * 0.5f;
            const float endAngle = currentAngle + (Math3D::PI * 2.0f);
            for(size_t i = 0; i < slices.size(); i++){
                float sweep = 0.0f;
                if((i + 1) == slices.size()){
                    // Ensure the final slice closes the circle to avoid tiny visual seams.
                    sweep = endAngle - currentAngle;
                }else{
                    sweep = (slices[i].ms / totalMs) * (Math3D::PI * 2.0f);
                }
                if(sweep <= 0.0f){
                    continue;
                }
                if((currentAngle + sweep) > endAngle){
                    sweep = endAngle - currentAngle;
                }
                sweeps[i] = std::max(0.0f, sweep);
                currentAngle += sweeps[i];
            }

            Graphics2D::SetBackgroundColor(g, Color::fromRGBA255(26, 26, 34, 225));
            Graphics2D::FillEllipse(g, pieCenterX - pieRadius, pieCenterY - pieRadius + pieDepth, pieDiameter, pieDiameter);

            float angleBottom = -Math3D::PI * 0.5f;
            for(size_t i = 0; i < slices.size(); i++){
                if(sweeps[i] <= 0.0f){
                    continue;
                }
                drawPieSlice(
                    g,
                    pieCenterX,
                    pieCenterY + pieDepth,
                    pieRadius,
                    angleBottom,
                    sweeps[i],
                    scaleColor(slices[i].color, 0.45f)
                );
                angleBottom += sweeps[i];
            }

            float angleTop = -Math3D::PI * 0.5f;
            for(size_t i = 0; i < slices.size(); i++){
                if(sweeps[i] <= 0.0f){
                    continue;
                }
                drawPieSlice(g, pieCenterX, pieCenterY, pieRadius, angleTop, sweeps[i], slices[i].color);
                angleTop += sweeps[i];
            }

            Graphics2D::SetBackgroundColor(g, Color::fromRGBA255(32, 32, 40, 255));
            Graphics2D::DrawEllipse(g, pieCenterX - pieRadius, pieCenterY - pieRadius, pieDiameter, pieDiameter);

            const float labelX = legendStartX + 15.0f;
            const float legendMaxY = y + h - panelPad;
            std::array<int, kSliceCount> legendOrder{};
            for(size_t i = 0; i < legendOrder.size(); i++){
                legendOrder[i] = static_cast<int>(i);
            }
            std::sort(legendOrder.begin(), legendOrder.end(), [&](int a, int b){
                return slices[(size_t)a].ms > slices[(size_t)b].ms;
            });

            for(size_t row = 0; row < legendOrder.size(); row++){
                const size_t i = static_cast<size_t>(legendOrder[row]);
                const float rowTop = legendY + (static_cast<float>(row) * legendLineH);
                if((rowTop + legendLineH) > legendMaxY){
                    break;
                }

                const float textY = rowTop + textBaselineOffset;
                const float pct = (slices[i].ms / totalMs) * 100.0f;
                const float chipSize = 9.0f;
                const float chipY = rowTop + ((legendLineH - chipSize) * 0.5f);

                Graphics2D::SetBackgroundColor(g, Color::fromRGBA255(18, 20, 26, 255));
                Graphics2D::DrawRect(g, legendStartX - 1.0f, chipY - 1.0f, chipSize + 2.0f, chipSize + 2.0f);
                Graphics2D::SetBackgroundColor(g, slices[i].color);
                Graphics2D::FillRect(g, legendStartX, chipY, chipSize, chipSize);

                Graphics2D::SetForegroundColor(g, Color::WHITE);
                char labelText[64];
                std::snprintf(
                    labelText,
                    sizeof(labelText),
                    "[%d] %-7s %5.1f%%",
                    static_cast<int>(row + 1),
                    slices[i].label,
                    pct
                );
                Graphics2D::DrawString(g, labelText, labelX, textY, true);
            }
        }

    private:
        static constexpr int kSliceCount = 6;

        struct Sample {
            float shadowMs = 0.0f;
            float drawMs = 0.0f;
            float postFxMs = 0.0f;
            float renderMs = 0.0f;
            float renderSceneMs = 0.0f;
            float renderBlitMs = 0.0f;
            float renderImGuiMs = 0.0f;
            float renderWaitMs = 0.0f;
            float swapMs = 0.0f;
        };

        struct Slice {
            const char* label;
            float ms;
            Math3D::Vec4 color;
        };

        Sample sample{};
        bool captureEnabled = false;
        bool hasSample = false;
        float sampleAccumSec = 0.0f;
        float sampleIntervalSec = 0.0f;
        float smoothing = 0.35f;

        static float clampNonNegative(float value){
            return std::max(0.0f, value);
        }

        float blend(float current, float target) const{
            return current + ((target - current) * smoothing);
        }

        static Math3D::Vec4 scaleColor(const Math3D::Vec4& color, float factor){
            return Math3D::Vec4(
                Math3D::Clamp<float>(color.x * factor, 0.0f, 1.0f),
                Math3D::Clamp<float>(color.y * factor, 0.0f, 1.0f),
                Math3D::Clamp<float>(color.z * factor, 0.0f, 1.0f),
                color.w
            );
        }

        static const char* vsyncModeToString(VSyncMode mode){
            switch(mode){
                case VSyncMode::On: return "on";
                case VSyncMode::Adaptive: return "adaptive";
                case VSyncMode::Off:
                default: return "off";
            }
        }

        static void drawPieSlice(Graphics2D& g,
                                 float cx,
                                 float cy,
                                 float radius,
                                 float startRad,
                                 float sweepRad,
                                 const Math3D::Vec4& color){
            if(sweepRad <= 0.0f || radius <= 0.0f){
                return;
            }

            const int segments = std::max(1, static_cast<int>(std::ceil((sweepRad / (Math3D::PI * 2.0f)) * 96.0f)));
            const float step = sweepRad / static_cast<float>(segments);

            Graphics2D::SetBackgroundColor(g, color);
            for(int i = 0; i < segments; i++){
                float a0 = startRad + (step * static_cast<float>(i));
                float a1 = a0 + step;
                float x0 = cx + (std::cos(a0) * radius);
                float y0 = cy + (std::sin(a0) * radius);
                float x1 = cx + (std::cos(a1) * radius);
                float y1 = cy + (std::sin(a1) * radius);
                Graphics2D::FillTriangle(g, cx, cy, x0, y0, x1, y1);
            }
        }
};

#endif // PROFILERPIECHART_H
