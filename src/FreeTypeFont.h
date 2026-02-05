#ifndef FREETYPE_FONT_H
#define FREETYPE_FONT_H

#include <ft2build.h> // Free Type Headers.
#include FT_FREETYPE_H
#include "Font.h"

class FreeTypeFont : public Font {
    public:
        FreeTypeFont() : Font() {};
        
        // Override the pure virtual functions from Base Font
        void _initFont() override;
        void drawText(std::string text, Math3D::Vec2 position, PCamera camera, Color color, bool useCache) override;
};

#endif