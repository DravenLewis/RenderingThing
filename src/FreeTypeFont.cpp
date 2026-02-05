#include "FreeTypeFont.h"
#include "MaterialDefaults.h"

#include <cmath>

void FreeTypeFont::_initFont(){

    if(!assetPtr){
        Font::FontLogger.Log(LOG_ERRO, "[Free Type] Asset was null");
        return;
    }

    FT_Library ft;
    if(FT_Init_FreeType(&ft)){
        Font::FontLogger.Log(LOG_ERRO, "[Free Type] Could not init freetype library");
        return;
    }

    BinaryBuffer rawData = assetPtr->asRaw();
    if(rawData.empty()){
        Font::FontLogger.Log(LOG_ERRO, "[Free Type] Font asset empty.");
        FT_Done_FreeType(ft);
        return;
    }

    FT_Face face;
    if(FT_New_Memory_Face(ft, reinterpret_cast<const FT_Byte*>(rawData.data()),(FT_Long) rawData.size(), 0, &face)){
        Font::FontLogger.Log(LOG_ERRO, "[Free Type] Could not load font face");
        FT_Done_FreeType(ft);
        return;
    }

    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(this->fontSize));

    std::vector<unsigned char> bitmapBuffer(FONT_BITMAP_W * FONT_BITMAP_H, 0);

    int penX = 0, penY = 0, currentRowHeight = 0;

    for(unsigned char c = 32; c < 128; c++){
        if(FT_Load_Char(face, c, FT_LOAD_RENDER)){
            continue; // we alreayd have it.
        }

        int w = face->glyph->bitmap.width;
        int h = face->glyph->bitmap.rows;

        if(penX + w >= FONT_BITMAP_W){
            penX = 0;
            penY += currentRowHeight + 1;
            currentRowHeight = 0;
        }

        if(penY + h > FONT_BITMAP_H){
            Font::FontLogger.Log(LOG_WARN, "[Free Type] Font Bitmap Overflow. Texture Size is too small; Glyphs may be truncated.");
            break; // out of view, dont even bother.
        }

        for(int row = 0; row < h; row++){
            for(int col = 0; col < w; col++){
                int index = (penY + row) * FONT_BITMAP_W + (penX + col);
                if(index < bitmapBuffer.size()){
                    bitmapBuffer[index] = face->glyph->bitmap.buffer[row * w + col];
                }
            }
        }

        FontCharacter& ch = this->characters[c];

        ch.sizeX = static_cast<float>(w);
        ch.sizeY = static_cast<float>(h);
        ch.bearingX = static_cast<float>(face->glyph->bitmap_left);
        ch.bearingY = static_cast<float>(face->glyph->bitmap_top);
        ch.advance = static_cast<float>(face->glyph->advance.x >> 6);

        ch.u0 = (float) penX / (float) FONT_BITMAP_W;
        ch.v0 = (float) penY / (float) FONT_BITMAP_H;
        ch.u1 = (float) (penX + w) / (float) FONT_BITMAP_W;
        ch.v1 = (float) (penY + h) / (float) FONT_BITMAP_H;

        penX += w + 1;
        if(h > currentRowHeight) currentRowHeight = h;
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    this->textureAtlasPtr = Texture::CreateFromAlphaBuffer(FONT_BITMAP_W,FONT_BITMAP_H, bitmapBuffer.data());
    this->textureAtlasPtr->bind();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    this->textureAtlasPtr->unbind();

    Font::FontLogger.Log(LOG_INFO,"Freetype atlas generated for font: %s", assetPtr->getFileHandle()->getFileName().c_str());
}

void FreeTypeFont::drawText(std::string text, Math3D::Vec2 position, PCamera camera, Color color, bool useCache){
    if(!this->textureAtlasPtr || !camera)  return;
    constexpr size_t MAX_TEXT_CACHE = 500;
    constexpr size_t TRIM_BATCH = 100;
    if(this->meshCache.size() > MAX_TEXT_CACHE){
        size_t toRemove = Math3D::Min(TRIM_BATCH, this->meshCache.size());
        for(size_t i = 0; i < toRemove; ++i){
            auto it = meshCache.begin();
            if(it == meshCache.end()) break;
            meshCache.erase(it);
        }
    }

    TextCacheKey key = {text, color};
    std::shared_ptr<ModelPart> meshToDraw = nullptr;

    // 1. Check Base Class Cache
    if(useCache){
        auto it = meshCache.find(key);
        if(it != meshCache.end()){
            meshToDraw = it->second; // We already have this mesh, no need to generate it.
        }
    }

    if(!meshToDraw){ // we dint have a mesh time to create one.
        auto mat = MaterialDefaults::ImageMaterial::Create(this->textureAtlasPtr, color);
        auto factory = ModelPartFactory::Create(mat);

        float x = 0, y = 0;
        const float lineHeight = std::ceil(this->fontSize);

        for(unsigned char c : text){
            if(c == '\r') continue;
            if(c == '\n'){
                x = 0;
                y += lineHeight;
                continue;
            }
            if(c < 32 || c >= 128) continue; // we dont need the stop codes like NULL or EOL / EOF.

            const FontCharacter& ch = this->characters[c];

            float xpos = std::round(x + ch.bearingX);
            float ypos = std::round(y - ch.bearingY);
            float w = ch.sizeX;
            float h = ch.sizeY;

            int v0, v1, v2, v3;

            factory
                .addVertex(Vertex::Build(Math3D::Vec3(xpos, ypos, 0))
                    .UV(ch.u0,ch.v0)
                    .Norm(0,0,1)
                    .Col(Color::WHITE),
                &v0)

                .addVertex(Vertex::Build(Math3D::Vec3(xpos + w, ypos, 0))
                    .UV(ch.u1,ch.v0)
                    .Norm(0,0,1)
                    .Col(Color::WHITE),
                &v1)

                .addVertex(Vertex::Build(Math3D::Vec3(xpos + w  , ypos + h, 0))
                    .UV(ch.u1,ch.v1)
                    .Norm(0,0,1)
                    .Col(Color::WHITE),
                &v2)

                .addVertex(Vertex::Build(Math3D::Vec3(xpos, ypos + h, 0))
                    .UV(ch.u0,ch.v1)
                    .Norm(0,0,1)
                    .Col(Color::WHITE)
                ,&v3);

            factory.defineFace(v0,v3,v2,v1);

            x += ch.advance;
        
        }

        meshToDraw = factory.assemble();
        if(useCache) meshCache[key] = meshToDraw;
    }

    // Draw the String.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST); 
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    float sx = std::floor(position.x);
    float sy = std::floor(position.y);

    Math3D::Transform t;
    t.setPosition(Math3D::Vec3(sx, sy, 0));
    
    if (meshToDraw) {
        meshToDraw->draw(t.toMat4(), camera->getViewMatrix(), camera->getProjectionMatrix());
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
}
