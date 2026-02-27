#ifndef FRAMETIMEGRAPH_H
#define FRAMETIMEGRAPH_H

#include "Rendering/Core/Graphics2D.h"
#include "Scene/Scene.h"
#include "Engine/Core/GameEngine.h"
#include <cstdio>
#include <algorithm>

class FrameTimeGraph {
public:
    inline static constexpr int MAX_SAMPLES = 500;

    float samples[MAX_SAMPLES];
    int head = 0;
    float maxSeen = 0.0f;

    // Thresholds in ms
    float greenLimit = 16.66f;   // 60fps
    float yellowLimit = 33.33f;  // 30fps

    // --- Global Statistics (Lifecycle Cache) ---
    double globalTime = 0.0;        // Total app runtime in seconds
    double lastPeakTime = -1.0;     // Timestamp of the previous red spike
    double totalIntervalSum = 0.0;  // Sum of all gaps between peaks
    int peakIntervalCount = 0;      // How many gaps we have measured
    double lastTextRefreshTime = -1.0;
    double textRefreshIntervalSec = 0.0; // 0 = refresh every frame
    char dtLine[256] = {};
    char ecsLine[256] = {};
    char engineLine[256] = {};
    char renderBreakdownLine[256] = {};
    float graphSampleAccumTime = 0.0f;
    float graphSampleAccumDt = 0.0f;
    int graphSampleAccumFrames = 0;
    float overlayRefreshAccum = 0.0f;
    float graphSampleIntervalSec = 0.0f;   // 0 = every frame (responsive graph); text/stat polling is still throttled
    float overlayRefreshIntervalSec = 0.0f; // 0 = poll sources every frame
    bool captureEnabled = false;

    struct EcsInfo {
        float snapshotMs = 0.0f;
        float shadowMs = 0.0f;
        float drawMs = 0.0f;
        float postFxMs = 0.0f;
        int drawCount = 0;
        int lightCount = 0;
        int postFxEffectCount = 0;
        bool hasData = false;
    };

    EcsInfo ecsInfo{};

    struct EngineInfo {
        float updateMs = 0.0f;
        float updateWaitMs = 0.0f;
        float renderMs = 0.0f;
        float renderWaitMs = 0.0f;
        float renderSceneMs = 0.0f;
        float renderBlitMs = 0.0f;
        float renderImGuiMs = 0.0f;
        float swapMs = 0.0f;
        bool hasData = false;
    };

    EngineInfo engineInfo{};

    FrameTimeGraph(){
        for(int i = 0; i < MAX_SAMPLES; i++){
            samples[i] = 0.0f;
        }
    }

    void setCaptureEnabled(bool enabled){
        if(captureEnabled == enabled){
            return;
        }
        captureEnabled = enabled;
        graphSampleAccumTime = 0.0f;
        graphSampleAccumDt = 0.0f;
        graphSampleAccumFrames = 0;
        overlayRefreshAccum = enabled ? overlayRefreshIntervalSec : 0.0f; // force refresh on next update when enabled
    }

    void updateFromSources(float deltaTime, const Scene::DebugStats& stats, const GameEngine* engine = nullptr){
        if(!captureEnabled){
            return;
        }

        graphSampleAccumTime += deltaTime;
        graphSampleAccumDt += deltaTime;
        graphSampleAccumFrames += 1;
        if(graphSampleAccumTime >= graphSampleIntervalSec && graphSampleAccumFrames > 0){
            const float avgDt = graphSampleAccumDt / (float)graphSampleAccumFrames;
            push(avgDt);
            graphSampleAccumTime = 0.0f;
            graphSampleAccumDt = 0.0f;
            graphSampleAccumFrames = 0;
        }

        overlayRefreshAccum += deltaTime;
        if(overlayRefreshAccum < overlayRefreshIntervalSec){
            return;
        }

        setEcsInfo(
            stats.snapshotMs.load(std::memory_order_relaxed),
            stats.shadowMs.load(std::memory_order_relaxed),
            stats.drawMs.load(std::memory_order_relaxed),
            stats.postFxMs.load(std::memory_order_relaxed),
            stats.drawCount.load(std::memory_order_relaxed),
            stats.lightCount.load(std::memory_order_relaxed),
            stats.postFxEffectCount.load(std::memory_order_relaxed)
        );

        if(engine){
            setEngineInfo(
                engine->getLastUpdateMs(),
                engine->getLastUpdateWaitMs(),
                engine->getLastRenderMs(),
                engine->getLastRenderWaitMs(),
                engine->getLastRenderSceneMs(),
                engine->getLastRenderBlitMs(),
                engine->getLastRenderImGuiMs(),
                engine->getLastSwapMs()
            );
        }else{
            engineInfo.hasData = false;
        }

        overlayRefreshAccum = 0.0f;
    }

    void push(float dtSeconds){
        // 1. Update Global Timer
        globalTime += dtSeconds;

        float frameTimeMs = dtSeconds * 1000.0f;
        
        // 2. Standard Circular Buffer Logic
        samples[head] = frameTimeMs;
        head = (head + 1) % MAX_SAMPLES;
        if(frameTimeMs > maxSeen)
            maxSeen = frameTimeMs;

        // 3. Global Peak Stats Calculation
        // If this frame is a "Red Peak"
        if(frameTimeMs > yellowLimit) {
            // If we have seen a peak before (lastPeakTime is not -1)
            // We can now measure the distance between the previous one and this one
            if(lastPeakTime >= 0.0) {
                double gapSeconds = globalTime - lastPeakTime;
                totalIntervalSum += gapSeconds;
                peakIntervalCount++;
            }

            // Mark this moment as the new "last seen peak"
            lastPeakTime = globalTime;
        }
    }

    void setEcsInfo(float snapshotMs, float shadowMs, float drawMs, float postFxMs, int drawCount, int lightCount, int postFxEffectCount){
        ecsInfo.snapshotMs = snapshotMs;
        ecsInfo.shadowMs = shadowMs;
        ecsInfo.drawMs = drawMs;
        ecsInfo.postFxMs = postFxMs;
        ecsInfo.drawCount = drawCount;
        ecsInfo.lightCount = lightCount;
        ecsInfo.postFxEffectCount = postFxEffectCount;
        ecsInfo.hasData = true;
    }

    void setEngineInfo(float updateMs,
                       float updateWaitMs,
                       float renderMs,
                       float renderWaitMs,
                       float renderSceneMs,
                       float renderBlitMs,
                       float renderImGuiMs,
                       float swapMs){
        engineInfo.updateMs = updateMs;
        engineInfo.updateWaitMs = updateWaitMs;
        engineInfo.renderMs = renderMs;
        engineInfo.renderWaitMs = renderWaitMs;
        engineInfo.renderSceneMs = renderSceneMs;
        engineInfo.renderBlitMs = renderBlitMs;
        engineInfo.renderImGuiMs = renderImGuiMs;
        engineInfo.swapMs = swapMs;
        engineInfo.hasData = true;
    }

    void draw(Graphics2D& g, float x, float y, float w, float h){
        // Downsample bars to reduce debug overlay draw calls.
        const int barCount = std::max(1, std::min(MAX_SAMPLES, (int)(w / 3.0f)));
        float barWidth = w / (float)barCount;

        // --- Draw Graph ---
        for(int i = 0; i < barCount; i++) {
            const int start = (i * MAX_SAMPLES) / barCount;
            const int end = std::max(start + 1, ((i + 1) * MAX_SAMPLES) / barCount);
            float t = 0.0f;
            bool hasHead = false;
            for(int s = start; s < end && s < MAX_SAMPLES; ++s){
                if(samples[s] > t){
                    t = samples[s];
                }
                if(s == head){
                    hasHead = true;
                }
            }

            float norm = t / yellowLimit;
            if (norm > 1.0f) norm = 1.0f;
            float barHeight = norm * h;

            // Color logic
            if (t <= greenLimit)
                Graphics2D::SetBackgroundColor(g, Color::GREEN);
            else if (t <= yellowLimit)
                Graphics2D::SetBackgroundColor(g, Color::YELLOW);
            else
                Graphics2D::SetBackgroundColor(g, Color::RED);

            float px = x + (i * barWidth);
            float py = y + h - barHeight;

            // Cursor logic
            if(hasHead) {
                 Graphics2D::SetBackgroundColor(g, Color::WHITE); 
                 Graphics2D::DrawLine(g, px, y, px, y + h); 
                 continue; 
            }

            Graphics2D::DrawLine(g, px, y + h, px, py);
        }

        // --- Calculate Average from Cache ---
        float avgPeakDistSec = 0.0f;
        float avgPeakDistMs = 0.0f;
        
        if (peakIntervalCount > 0) {
            avgPeakDistSec = (float)(totalIntervalSum / peakIntervalCount);
            avgPeakDistMs = avgPeakDistSec * 1000.0f;
        }

        // --- Header Text ---
        int latestIdx = (head - 1 + MAX_SAMPLES) % MAX_SAMPLES;
        
        const bool refreshText = (lastTextRefreshTime < 0.0) || ((globalTime - lastTextRefreshTime) >= textRefreshIntervalSec);
        if(refreshText){
            std::snprintf(dtLine, sizeof(dtLine),
                "[DT Monitor] %.1f ms | Max: %.1f | Avg. Peak Time: %.1f ms (%.1f Seconds)",
                samples[latestIdx],
                maxSeen,
                avgPeakDistMs,
                avgPeakDistSec
            );

            if(ecsInfo.hasData){
                std::snprintf(ecsLine, sizeof(ecsLine),
                    "[ECS] Snapshot: %.1f ms | Shadow: %.1f ms | Draw: %.1f ms | PostFX: %.1f ms (%d) | DrawItems: %d | Lights: %d",
                    ecsInfo.snapshotMs,
                    ecsInfo.shadowMs,
                    ecsInfo.drawMs,
                    ecsInfo.postFxMs,
                    ecsInfo.postFxEffectCount,
                    ecsInfo.drawCount,
                    ecsInfo.lightCount
                );
            }else{
                ecsLine[0] = '\0';
            }

            if(engineInfo.hasData){
                std::snprintf(engineLine, sizeof(engineLine),
                    "[Engine] U: %.1f ms (wait %.1f) | R: %.1f ms (wait %.1f) | Swap/Present: %.1f ms",
                    engineInfo.updateMs,
                    engineInfo.updateWaitMs,
                    engineInfo.renderMs,
                    engineInfo.renderWaitMs,
                    engineInfo.swapMs
                );
                std::snprintf(renderBreakdownLine, sizeof(renderBreakdownLine),
                    "[Render] Scene: %.1f ms | Blit: %.1f ms | ImGui: %.1f ms",
                    engineInfo.renderSceneMs,
                    engineInfo.renderBlitMs,
                    engineInfo.renderImGuiMs
                );
            }else{
                engineLine[0] = '\0';
                renderBreakdownLine[0] = '\0';
            }

            lastTextRefreshTime = globalTime;
        }

        Graphics2D::SetBackgroundColor(g, Color::WHITE);
        Graphics2D::DrawString(g, dtLine, x, y - 14, true);

        if(ecsInfo.hasData){
            Graphics2D::SetBackgroundColor(g, Color::WHITE);
            Graphics2D::DrawString(g, ecsLine, x, y - 32, true);
        }

        if(engineInfo.hasData){
            Graphics2D::SetBackgroundColor(g, Color::WHITE);
            Graphics2D::DrawString(g, engineLine, x, y - 50, true);
            Graphics2D::DrawString(g, renderBreakdownLine, x, y - 68, true);
        }
    }
};

#endif // FRAMETIMEGRAPH_H
