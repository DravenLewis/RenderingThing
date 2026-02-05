#ifndef STB_TTF_H
#define STB_TTF_H

#include "STB/stb_truetype.h" // STB Headers.
#include "Font.h"

class TrueTypeFont : public Font{
    private:
        stbtt_packedchar cdata[96];
    public:
        TrueTypeFont() : Font() {};
        void _initFont() override;
        void drawText(std::string text, Math3D::Vec2 position, PCamera camera, Color color, bool useCache) override;
};


#endif //STB_TTF_H