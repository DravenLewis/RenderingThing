#ifndef FONT_H
#define FONT_H


#define ENGINE_STB_TRUETYPE  0x0FA25
#define ENGINE_FREETYPE  0x0E281

#define FONT_ENGINE ENGINE_FREETYPE

#include <memory>
#include <map>
#include <array>
#include <vector>

#include "Asset.h"
#include "Math.h"
#include "Drawable.h"
#include "ModelPart.h"
#include "Texture.h"
#include "Color.h"
#include "Camera.h"
#include "Logbot.h"


#define FONT_BITMAP_W 1024
#define FONT_BITMAP_H 1024


struct TextCacheKey{ // Store mesh of common reused texts, so we dont recreate them each frame, if the text changes.
    std::string text;
    Color color;

    bool operator<(const TextCacheKey& other) const {
        if(text != other.text) return text < other.text;
        return color.toRGBA32() < other.color.toRGBA32();
    }
};

struct FontCharacter{
    float u0, v0, u1, v1;
    float sizeX, sizeY;
    float bearingX, bearingY;
    float advance;
};

class Font{
    protected:
        PAsset assetPtr;
        PTexture textureAtlasPtr;
        PMaterial materialCachePtr;
        float fontSize;

        std::array<FontCharacter, 128> characters;
        std::map<TextCacheKey, std::shared_ptr<ModelPart>> meshCache;

        virtual void _initFont() = 0;
    public:

        inline static Logbot FontLogger = Logbot::CreateInstance("Font");

        Font() : fontSize(0) {};
        virtual ~Font() = default;


        virtual void drawText(std::string text = "", Math3D::Vec2 position = Math3D::Vec2(0,0), PCamera camera = nullptr, Color color = Color::WHITE, bool useCache = true) = 0;
        void clearCache(){
            this->meshCache.clear();
        }

        // The goal is to have multiple font engines for specific loading,
        // so i could do auto font = Font::Create<FreeTypeFont>(assetPtr, 24.0f)
        template<typename T>
        static inline std::shared_ptr<T> Create(PAsset asset, float fontSize = 18.0f){
            static_assert(std::is_base_of<Font, T>::value, "Class must inherit from Font.");

            if(!asset){
                FontLogger.Log(LOG_ERRO, "Asset was null");
                return nullptr;
            }

            if(!asset->loaded()){
                FontLogger.Log(LOG_ERRO, "Cannot create font from unloaded asset, make sure its loaded, call Asset::load() or AssetManager::getOrLoad()");
                return nullptr;
            }

            auto font = std::make_shared<T>();
            font->assetPtr = asset;
            font->fontSize = fontSize;
            font->_initFont();
    
            return font;
        }

        PAsset getAssetPointer(){
            return this->assetPtr;
        }
}; 

typedef std::shared_ptr<Font> PFont;

#endif // FONT_H
