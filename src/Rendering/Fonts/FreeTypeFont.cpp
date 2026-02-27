#include "Rendering/Fonts/FreeTypeFont.h"
#include "Rendering/Materials/MaterialDefaults.h"

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

FreeTypeFont::DeferredBatch& FreeTypeFont::getOrCreateBatch(const Color& color){
    const uint32_t colorKey = color.toRGBA32();
    for(auto& batch : deferredBatches){
        if(batch.colorKey == colorKey){
            return batch;
        }
    }

    deferredBatches.push_back(DeferredBatch{});
    DeferredBatch& created = deferredBatches.back();
    created.colorKey = colorKey;
    created.color = color;
    return created;
}

void FreeTypeFont::beginFrame(PCamera){
    frameActive = true;
    for(auto& batch : deferredBatches){
        batch.vertices.clear();
        batch.indices.clear();
    }
}

void FreeTypeFont::drawText(std::string text, Math3D::Vec2 position, PCamera camera, Color color, bool){
    if(!this->textureAtlasPtr || !camera || text.empty()){
        return;
    }

    if(!frameActive){
        beginFrame(camera);
    }

    DeferredBatch& batch = getOrCreateBatch(color);
    batch.vertices.reserve(batch.vertices.size() + (text.size() * 4));
    batch.indices.reserve(batch.indices.size() + (text.size() * 6));

    const float baseX = std::floor(position.x);
    const float baseY = std::floor(position.y);
    float penX = 0.0f;
    float penY = 0.0f;
    const float lineHeight = std::ceil(this->fontSize);

    for(unsigned char c : text){
        if(c == '\r'){
            continue;
        }
        if(c == '\n'){
            penX = 0.0f;
            penY += lineHeight;
            continue;
        }
        if(c < 32 || c >= 128){
            continue;
        }

        const FontCharacter& ch = this->characters[c];
        const float xpos = std::round(baseX + penX + ch.bearingX);
        const float ypos = std::round(baseY + penY - ch.bearingY);
        const float w = ch.sizeX;
        const float h = ch.sizeY;
        const uint32_t base = static_cast<uint32_t>(batch.vertices.size());

        batch.vertices.push_back(Vertex::Build(Math3D::Vec3(xpos,     ypos,     0)).UV(ch.u0, ch.v0).Norm(0,0,1).Col(Color::WHITE));
        batch.vertices.push_back(Vertex::Build(Math3D::Vec3(xpos + w, ypos,     0)).UV(ch.u1, ch.v0).Norm(0,0,1).Col(Color::WHITE));
        batch.vertices.push_back(Vertex::Build(Math3D::Vec3(xpos + w, ypos + h, 0)).UV(ch.u1, ch.v1).Norm(0,0,1).Col(Color::WHITE));
        batch.vertices.push_back(Vertex::Build(Math3D::Vec3(xpos,     ypos + h, 0)).UV(ch.u0, ch.v1).Norm(0,0,1).Col(Color::WHITE));

        batch.indices.push_back(base + 0);
        batch.indices.push_back(base + 3);
        batch.indices.push_back(base + 2);
        batch.indices.push_back(base + 2);
        batch.indices.push_back(base + 1);
        batch.indices.push_back(base + 0);

        penX += ch.advance;
    }
}

void FreeTypeFont::flushQueuedText(PCamera camera){
    if(!frameActive){
        return;
    }
    if(!camera){
        frameActive = false;
        return;
    }

    if(!transientTextPart){
        transientTextPart = std::make_shared<ModelPart>();
        transientTextPart->mesh = std::make_shared<Mesh>();
    }else if(!transientTextPart->mesh){
        transientTextPart->mesh = std::make_shared<Mesh>();
    }

    auto imageMat = Material::GetAs<MaterialDefaults::ImageMaterial>(this->materialCachePtr);
    if(!imageMat || !imageMat->getShader() || imageMat->getShader()->getID() == 0){
        this->materialCachePtr = MaterialDefaults::ImageMaterial::Create(this->textureAtlasPtr, Color::WHITE);
        imageMat = Material::GetAs<MaterialDefaults::ImageMaterial>(this->materialCachePtr);
    }
    if(!imageMat){
        frameActive = false;
        return;
    }

    imageMat->Tex = this->textureAtlasPtr;
    imageMat->UV = Math3D::Vec2(0.0f, 0.0f);
    transientTextPart->material = this->materialCachePtr;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    Math3D::Transform t;
    t.setPosition(Math3D::Vec3(0, 0, 0));
    const Math3D::Mat4 model = t.toMat4();
    const Math3D::Mat4 view = camera->getViewMatrix();
    const Math3D::Mat4 projection = camera->getProjectionMatrix();

    for(auto& batch : deferredBatches){
        if(batch.vertices.empty() || batch.indices.empty()){
            continue;
        }
        imageMat->Color = batch.color;
        transientTextPart->mesh->upload(batch.vertices, batch.indices, GL_STREAM_DRAW);
        transientTextPart->draw(model, view, projection);
    }

    frameActive = false;

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
}
