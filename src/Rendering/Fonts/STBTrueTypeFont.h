/**
 * @file src/Rendering/Fonts/STBTrueTypeFont.h
 * @brief Declarations for STBTrueTypeFont.
 */

#ifndef STB_TTF_H
#define STB_TTF_H

#include "STB/stb_truetype.h" // STB Headers.
#include "Rendering/Fonts/Font.h"

/// @brief Represents the TrueTypeFont type.
class TrueTypeFont : public Font{
    private:
        stbtt_packedchar cdata[96];
    public:
        /**
         * @brief Constructs a new TrueTypeFont instance.
         */
        TrueTypeFont() : Font() {};
        void _initFont() override;
        void drawText(std::string text, Math3D::Vec2 position, PCamera camera, Color color, bool useCache) override;
};


#endif //STB_TTF_H
