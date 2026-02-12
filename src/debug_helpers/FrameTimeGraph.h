#ifndef FRAMETIMEGRAPH_H
#define FRAMETIMEGRAPH_H

#include "Graphics2D.h"
#include <cstdio>

class FrameTimeGraph {
public:
    static const int MAX_SAMPLES = 500; 

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

    struct EcsInfo {
        float snapshotMs = 0.0f;
        float shadowMs = 0.0f;
        float drawMs = 0.0f;
        int drawCount = 0;
        int lightCount = 0;
        bool hasData = false;
    };

    EcsInfo ecsInfo{};

    FrameTimeGraph(){
        for(int i = 0; i < MAX_SAMPLES; i++){
            samples[i] = 0.0f;
        }
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

    void setEcsInfo(float snapshotMs, float shadowMs, float drawMs, int drawCount, int lightCount){
        ecsInfo.snapshotMs = snapshotMs;
        ecsInfo.shadowMs = shadowMs;
        ecsInfo.drawMs = drawMs;
        ecsInfo.drawCount = drawCount;
        ecsInfo.lightCount = lightCount;
        ecsInfo.hasData = true;
    }

    void draw(Graphics2D& g, float x, float y, float w, float h){
        float barWidth = w / (float)MAX_SAMPLES;

        // --- Draw Graph ---
        for (int i = 0; i < MAX_SAMPLES; i++) {
            float t = samples[i];

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
            if (i == head) {
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
        
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
            "[DT Monitor] %.2f ms | Max: %.2f | Avg. Peak Time: %.2f ms (%.2f Seconds)",
            samples[latestIdx],
            maxSeen,
            avgPeakDistMs, 
            avgPeakDistSec
        );

        Graphics2D::SetBackgroundColor(g, Color::WHITE);
        Graphics2D::DrawString(g, buffer, x, y - 14);

        if(ecsInfo.hasData){
            char ecsBuffer[256];
            snprintf(ecsBuffer, sizeof(ecsBuffer),
                "[ECS] Snapshot: %.2f ms | Shadow: %.2f ms | Draw: %.2f ms | DrawItems: %d | Lights: %d",
                ecsInfo.snapshotMs,
                ecsInfo.shadowMs,
                ecsInfo.drawMs,
                ecsInfo.drawCount,
                ecsInfo.lightCount
            );
            Graphics2D::SetBackgroundColor(g, Color::WHITE);
            Graphics2D::DrawString(g, ecsBuffer, x, y - 32);
        }
    }
};

#endif // FRAMETIMEGRAPH_H
