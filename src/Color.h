#ifndef COLOR_H
#define COLOR_H

#include "Math.h"
#include "math.h"

#include <cstdint>

struct Color : public Math3D::Vec4{

    //Vec4(float x = 0.f, float y = 0.f, float z = 0.f, float w = 1.f) : x(x), y(y), z(z), w(w) {}
    //Vec4(const Vec3& v, float w = 1.f) : x(v.x), y(v.y), z(v.z), w(w) {}
    //Vec4(const glm::vec4& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}

    Color(float r = 0.f, float g = 0.f, float b = 0.f, float a = 1.0f) : Vec4(r,g,b,a) {};
    Color(const Math3D::Vec4& vec) : Vec4(vec.x,vec.y,vec.z,vec.w) {};

    uint32_t toRGBA32() const {
        uint32_t col = 0;
        col += static_cast<int>(std::round(Math3D::Clamp<float>(x,0,1) * 255.0f));
        col = (col << 8) + static_cast<int>(std::round(Math3D::Clamp<float>(y,0.0f,1.0f) * 255.0f));
        col = (col << 8) + static_cast<int>(std::round(Math3D::Clamp<float>(z,0.0f,1.0f) * 255.0f));
        col = (col << 8) + static_cast<int>(std::round(Math3D::Clamp<float>(w,0.0f,1.0f) * 255.0f));

        return col;
    }

    void setRed(float value){ x = Math3D::Clamp<float>(value,0.0f,1.0f);}
    void setGreen(float value){ y = Math3D::Clamp<float>(value,0.0f,1.0f);}
    void setBlue(float value){ z = Math3D::Clamp<float>(value,0.0f,1.0f);}
    void setAlpha(float value){ w = Math3D::Clamp<float>(value,0.0f,1.0f);}

    float getRed() {return x;};
    float getGreen() {return y;};
    float getBlue() {return z;};
    float getAlpha() {return w;};

    static Math3D::Vec4 fromRGBA255(int r, int g, int b, int a = 255){
        float nr = r / 255.0f;
        float ng = g / 255.0f;
        float nb = b / 255.0f;
        float na = a / 255.0f;

        return Math3D::Vec4(nr,ng,nb,na);
    }

    static Math3D::Vec4 fromRGBA32(uint32_t rgba){
        int r = ((rgba) >> 24) & 0xFF;
        int g = ((rgba) >> 16) & 0xFF;
        int b = ((rgba) >> 8) & 0xFF;
        int a = ((rgba)) & 0xFF;

        return fromRGBA255(r,g,b,a);
    }

    static Math3D::Vec4 fromRGB24(uint32_t rgb){
        int r = ((rgb) >> 16) & 0xFF;
        int g = ((rgb) >> 8) & 0xFF;
        int b = ((rgb)) & 0xFF;
        int a = 255;

        return fromRGBA255(r,g,b,a);
    }

    inline static Color fromVec4(Vec4 colorVec){
        Color color;
        color.x = colorVec.x;
        color.y = colorVec.y;
        color.z = colorVec.z;
        color.w = colorVec.w;

        return color;
    }

    // Add this inside struct Color
    bool operator==(const Color& other) const {
        // Compare the 4 float components (inherited from Vec4)
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }

    bool operator!=(const Color& other) const {
        return !(*this == other);
    }

    inline static const Math3D::Vec4 RED = Color::fromRGBA32(0xFF0000FF);
    inline static const Math3D::Vec4 GREEN = Color::fromRGBA32(0x00FF00FF);
    inline static const Math3D::Vec4 BLUE = Color::fromRGBA32(0x0000FFFF);
    inline static const Math3D::Vec4 MAGENTA = Color::fromRGBA32(0xFF00FFFF);
    inline static const Math3D::Vec4 YELLOW = Color::fromRGBA32(0xFFFF00FF);
    inline static const Math3D::Vec4 CYAN = Color::fromRGBA32(0x00FFFFFF);
    inline static const Math3D::Vec4 BLACK = Color::fromRGBA32(0x000000FF);
    inline static const Math3D::Vec4 WHITE = Color::fromRGBA32(0xFFFFFFFF);
    inline static const Math3D::Vec4 CLEAR = Color::fromRGBA32(0x00000000);
    inline static const Math3D::Vec4 GRAY         = Color::fromRGBA32(0x808080FF);
    inline static const Math3D::Vec4 LIGHT_GRAY   = Color::fromRGBA32(0xD3D3D3FF);
    inline static const Math3D::Vec4 DARK_GRAY    = Color::fromRGBA32(0x404040FF);
    inline static const Math3D::Vec4 SILVER       = Color::fromRGBA32(0xC0C0C0FF);
    inline static const Math3D::Vec4 CHARCOAL     = Color::fromRGBA32(0x36454FFF);
    inline static const Math3D::Vec4 BEIGE        = Color::fromRGBA32(0xF5F5DCFF);
    inline static const Math3D::Vec4 ORANGE       = Color::fromRGBA32(0xFFA500FF);
    inline static const Math3D::Vec4 GOLD         = Color::fromRGBA32(0xFFD700FF);
    inline static const Math3D::Vec4 CORAL        = Color::fromRGBA32(0xFF7F50FF);
    inline static const Math3D::Vec4 CRIMSON      = Color::fromRGBA32(0xDC143CFF);
    inline static const Math3D::Vec4 MAROON       = Color::fromRGBA32(0x800000FF);
    inline static const Math3D::Vec4 TOMATO       = Color::fromRGBA32(0xFF6347FF);
    inline static const Math3D::Vec4 SALMON       = Color::fromRGBA32(0xFA8072FF);
    inline static const Math3D::Vec4 BROWN        = Color::fromRGBA32(0xA52A2AFF);
    inline static const Math3D::Vec4 NAVY         = Color::fromRGBA32(0x000080FF);
    inline static const Math3D::Vec4 SKY_BLUE     = Color::fromRGBA32(0x87CEEBFF);
    inline static const Math3D::Vec4 STEEL_BLUE   = Color::fromRGBA32(0x4682B4FF);
    inline static const Math3D::Vec4 TEAL         = Color::fromRGBA32(0x008080FF);
    inline static const Math3D::Vec4 TURQUOISE    = Color::fromRGBA32(0x40E0D0FF);
    inline static const Math3D::Vec4 FOREST_GREEN = Color::fromRGBA32(0x228B22FF);
    inline static const Math3D::Vec4 LIME         = Color::fromRGBA32(0x32CD32FF);
    inline static const Math3D::Vec4 MINT         = Color::fromRGBA32(0x98FF98FF);
    inline static const Math3D::Vec4 OLIVE        = Color::fromRGBA32(0x808000FF);
    inline static const Math3D::Vec4 PURPLE       = Color::fromRGBA32(0x800080FF);
    inline static const Math3D::Vec4 VIOLET       = Color::fromRGBA32(0xEE82EEFF);
    inline static const Math3D::Vec4 INDIGO       = Color::fromRGBA32(0x4B0082FF);
    inline static const Math3D::Vec4 SLATE_BLUE   = Color::fromRGBA32(0x6A5ACDFF);
    inline static const Math3D::Vec4 LAVENDER     = Color::fromRGBA32(0xE6E6FAFF);
    inline static const Math3D::Vec4 PINK         = Color::fromRGBA32(0xFFC0CBFF);
    inline static const Math3D::Vec4 HOT_PINK     = Color::fromRGBA32(0xFF69B4FF);
};


#endif //COLOR_H