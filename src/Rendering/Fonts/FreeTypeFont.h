/**
 * @file src/Rendering/Fonts/FreeTypeFont.h
 * @brief Declarations for FreeTypeFont.
 */

#ifndef FREETYPE_FONT_H
#define FREETYPE_FONT_H

#include <ft2build.h> // Free Type Headers.
#include FT_FREETYPE_H
#include "Rendering/Fonts/Font.h"

/// @brief Represents the FreeTypeFont type.
class FreeTypeFont : public Font {
    public:
        /**
         * @brief Constructs a new FreeTypeFont instance.
         */
        FreeTypeFont() : Font() {};
        
        // Override the pure virtual functions from Base Font
        void _initFont() override;
        void drawText(std::string text, Math3D::Vec2 position, PCamera camera, Color color, bool useCache) override;
        bool supportsQueuedDrawing() const override { return true; }
        void beginFrame(PCamera camera) override;
        void flushQueuedText(PCamera camera) override;

    private:
        /// @brief Holds data for DeferredBatch.
        struct DeferredBatch {
            uint32_t colorKey = 0;
            Color color = Color::WHITE;
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
        };

        DeferredBatch& getOrCreateBatch(const Color& color);
        std::shared_ptr<ModelPart> transientTextPart;
        std::vector<DeferredBatch> deferredBatches;
        bool frameActive = false;
};

#endif
