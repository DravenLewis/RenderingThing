#include "STBTrueTypeFont.h"

#include "Logbot.h"
#include "MaterialDefaults.h"

#ifndef STB_TRUETYPE_IMPLEMENTATION
    #define STB_TRUETYPE_IMPLEMENTATION
    #include "STB/stb_truetype.h"
#endif


void TrueTypeFont::_initFont(){
    

    std::memset(cdata,0,sizeof(cdata)); // Init Buffers.
    

    // 1. Setup the Pack Context
    stbtt_pack_context pc;
    std::vector<unsigned char> bitmap(FONT_BITMAP_W * FONT_BITMAP_H);

    std::memset(bitmap.data(), 0, bitmap.size()); // 0 the buffer.

    // Initialize the packing context
    // 1 = stride (usually 0 or 1), 1 = padding (This is the fix! 1 pixel border)
    // nullptr = allocator (default)
    if (!stbtt_PackBegin(&pc, bitmap.data(), FONT_BITMAP_W, FONT_BITMAP_H, 0, 1, nullptr)) {
        Font::FontLogger.Log(LOG_ERRO, "Failed to initialize font packing context");
        return;
    }

    // 2. Load the font data
    BinaryBuffer rawData = assetPtr->asRaw();
    if(rawData.empty()){
        Font::FontLogger.Log(LOG_ERRO, "Font asset empty.");
        stbtt_PackEnd(&pc);
        return;
    }
    const unsigned char* ttf_data = reinterpret_cast<const unsigned char*>(rawData.data());

    // 3. Pack the font range
    // We pass the 'cdata' array to be filled with coordinate info
    // Range: 32 to 128 (covering standard ASCII)
    // The 'cdata' array must be large enough! (stbtt_packedchar cdata[96])
    // Note: Your current cdata is likely stbtt_bakedchar. You need to change it to stbtt_packedchar.
    int success = stbtt_PackFontRange(&pc, ttf_data, 0, this->fontSize, 32, 96, reinterpret_cast<stbtt_packedchar*>(cdata));
    
    if(!success){
        Font::FontLogger.Log(LOG_WARN, "Font Bitmap Overflow. Texture Size is too small.");
    }

    stbtt_PackEnd(&pc);

    // 4. Create Texture (Same as before)
    this->textureAtlasPtr = Texture::CreateFromAlphaBuffer(FONT_BITMAP_W,FONT_BITMAP_H, bitmap.data());

    this->textureAtlasPtr->bind();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    this->textureAtlasPtr->unbind();
}

void TrueTypeFont::drawText(std::string text, Math3D::Vec2 position, PCamera camera, Color color, bool useCache){
    if(!this->textureAtlasPtr || !camera) return;

    if(this->meshCache.size() > 500){
        meshCache.clear();
    }

    TextCacheKey key = {text, color};
    std::shared_ptr<ModelPart> meshToDraw = nullptr;

    if(useCache){
        auto it = meshCache.find(key);
        if(it != meshCache.end()){
            meshToDraw = it->second;
        }
    }

    if(!meshToDraw){
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
            if(c < 32 || c >= 128) continue;

            stbtt_aligned_quad q;
            stbtt_GetPackedQuad(cdata, FONT_BITMAP_W, FONT_BITMAP_H, c - 32, &x, &y, &q, 0); 

            int v0, v1, v2, v3;

            factory
                .addVertex(Vertex::Build(Math3D::Vec3(q.x0, q.y0, 0)).UV(q.s0,q.t0).Norm(0,0,1).Col(Color::WHITE),&v0)
                .addVertex(Vertex::Build(Math3D::Vec3(q.x1, q.y0, 0)).UV(q.s1,q.t0).Norm(0,0,1).Col(Color::WHITE),&v1)
                .addVertex(Vertex::Build(Math3D::Vec3(q.x1, q.y1, 0)).UV(q.s1,q.t1).Norm(0,0,1).Col(Color::WHITE),&v2)
                .addVertex(Vertex::Build(Math3D::Vec3(q.x0, q.y1, 0)).UV(q.s0,q.t1).Norm(0,0,1).Col(Color::WHITE),&v3);
        
            factory.defineFace(v0,v3,v2,v1);
        }

        meshToDraw = factory.assemble();
        if(useCache)
            meshCache[key] = meshToDraw;
    }


    // 3. Render State Setup
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST); 
    glDisable(GL_CULL_FACE);

    // 4. Calculate Transform
    float sx = std::floor(position.x);
    float sy = std::floor(position.y);

    Math3D::Transform t;
    t.setPosition(Math3D::Vec3(sx, sy, 0));
    
    // 5. Draw
    meshToDraw->draw(t.toMat4(), camera->getViewMatrix(), camera->getProjectionMatrix());

    // 6. Restore State
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

