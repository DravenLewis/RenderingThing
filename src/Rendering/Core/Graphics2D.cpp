#include "Rendering/Core/Graphics2D.h"

namespace {
    std::shared_ptr<Material> CreateGraphics2DBatchMaterial(){
        static const std::string kVertShader = R"(#version 410 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 aTexCoord;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec4 v_color;
out vec2 v_uv;

void main() {
    v_color = aColor;
    v_uv = aTexCoord;
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
}
)";

        static const std::string kFragShader = R"(#version 410 core
in vec4 v_color;
in vec2 v_uv;

out vec4 FragColor;

uniform sampler2D u_texture;
uniform int u_useTexture;

void main() {
    vec4 color = v_color;
    if(u_useTexture != 0){
        color *= texture(u_texture, v_uv);
    }
    FragColor = color;
}
)";

        auto program = std::make_shared<ShaderProgram>();
        program->setVertexShader(kVertShader);
        program->setFragmentShader(kFragShader);

        auto material = std::make_shared<Material>(program);
        material->set<int>("u_useTexture", 0);
        material->set<int>("u_receiveShadows", 0);
        return material;
    }
}

Graphics2D::Graphics2D(PScreen screenContext){
    this->target = screenContext;
    this->uiCamera = screenContext->getCamera();

    this->imageMaterial = MaterialDefaults::ImageMaterial::Create(Texture::CreateEmpty(1,1));
    this->colorMaterial = MaterialDefaults::ColorMaterial::Create(Color::WHITE);
    this->batchMaterial = CreateGraphics2DBatchMaterial();

    //this->unitQuad = ModelPartPrefabs::MakePlane(1,1,colorMaterial);
    //this->unitQuad->localTransform = Math3D::Transform();
    //this->unitQuad->localTransform.setRotation(90.0f,0,0);
   
    // FIX: Do NOT use MakePlane. It builds a floor (XZ). 
    // We need a Wall (XY) for the screen.
    auto factory = ModelPartFactory::Create(colorMaterial);
    int v0, v1, v2, v3;

    // Create vertices directly on the XY plane.
    // Size is 1.0 (from -0.5 to 0.5).
    // Z is 0.0 (perfect for Ortho camera).
    factory
        .addVertex(Vertex::Build(Math3D::Vec3(-0.5f, -0.5f, 0.0f)).UV(0,0), &v0) // Bottom-Left
        .addVertex(Vertex::Build(Math3D::Vec3( 0.5f, -0.5f, 0.0f)).UV(1,0), &v1) // Bottom-Right
        .addVertex(Vertex::Build(Math3D::Vec3( 0.5f,  0.5f, 0.0f)).UV(1,1), &v2) // Top-Right
        .addVertex(Vertex::Build(Math3D::Vec3(-0.5f,  0.5f, 0.0f)).UV(0,1), &v3) // Top-Left
        .defineFace(v0, v1, v2, v3);

    this->unitQuad = factory.assemble();

    // CRITICAL: Ensure the quad has Identity transform.
    // No hidden rotations, no hidden scales.
    this->unitQuad->localTransform = Math3D::Transform();

    this->unitCircle = ModelPartPrefabs::MakeCirclePlane(0.5f, 64, colorMaterial); // Unit, so radius must be 1/2
    ensureBatchResources();
    batchVertices.reserve(maxBatchQuads * 4);
    batchIndices.reserve(maxBatchQuads * 6);

    this->context.fontSize.onChange([this](int oldValue, int newValue) -> bool{

        int value = Math3D::Max(1, newValue);

        auto fontPtr = this->context.font;
        fontPtr.set(Font::Create<FreeTypeFont>(fontPtr.get()->getAssetPointer(), (float) value));

        return true;
    });
}

void Graphics2D::resize(int w, int h){
    this->target->resize(w, h);
}

void Graphics2D::begin(){
    resetBatch();
    this->target->bind();
}

void Graphics2D::end(){
    flushBatch();
    this->target->unbind();
}

GraphicsContext& Graphics2D::getContext(){
    return this->context;
}

void Graphics2D::ensureBatchResources(){
    if(batchPart && batchPart->mesh && batchMaterial){
        return;
    }

    if(!batchPart){
        batchPart = std::make_shared<ModelPart>();
        batchPart->localTransform = Math3D::Transform();
    }
    if(!batchPart->mesh){
        batchPart->mesh = std::make_shared<Mesh>();
    }
    if(!batchMaterial){
        batchMaterial = CreateGraphics2DBatchMaterial();
    }
    batchPart->material = batchMaterial;
}

void Graphics2D::resetBatch(){
    batchVertices.clear();
    batchIndices.clear();
    batchMode = BatchMode2D::None;
    batchTexture.reset();
}

void Graphics2D::beginBatch(BatchMode2D mode, PTexture texture){
    if(mode == BatchMode2D::None){
        return;
    }

    const bool modeChanged = (batchMode != mode);
    const bool textureChanged = (mode == BatchMode2D::Textured && batchTexture != texture);
    if(modeChanged || textureChanged){
        flushBatch();
    }

    batchMode = mode;
    batchTexture = (mode == BatchMode2D::Textured) ? texture : nullptr;
}

void Graphics2D::submitQuad(
    const Math3D::Vec3& p0, const Math3D::Vec3& p1, const Math3D::Vec3& p2, const Math3D::Vec3& p3,
    const Math3D::Vec2& uv0, const Math3D::Vec2& uv1, const Math3D::Vec2& uv2, const Math3D::Vec2& uv3,
    const Math3D::Vec4& color,
    BatchMode2D mode,
    PTexture texture
){
    if(mode == BatchMode2D::Textured && !texture){
        return;
    }

    ensureBatchResources();
    beginBatch(mode, texture);

    if((batchVertices.size() + 4) > (maxBatchQuads * 4)){
        flushBatch();
        beginBatch(mode, texture);
    }

    const uint32_t baseIndex = static_cast<uint32_t>(batchVertices.size());

    batchVertices.push_back(Vertex::Build(p0, color, Math3D::Vec3(0,0,1), uv0));
    batchVertices.push_back(Vertex::Build(p1, color, Math3D::Vec3(0,0,1), uv1));
    batchVertices.push_back(Vertex::Build(p2, color, Math3D::Vec3(0,0,1), uv2));
    batchVertices.push_back(Vertex::Build(p3, color, Math3D::Vec3(0,0,1), uv3));

    batchIndices.push_back(baseIndex + 0);
    batchIndices.push_back(baseIndex + 1);
    batchIndices.push_back(baseIndex + 2);
    batchIndices.push_back(baseIndex + 2);
    batchIndices.push_back(baseIndex + 3);
    batchIndices.push_back(baseIndex + 0);
}

void Graphics2D::flushBatch(){
    if(batchVertices.empty() || batchIndices.empty()){
        return;
    }

    ensureBatchResources();

    if(!batchPart || !batchPart->mesh || !batchPart->material || !uiCamera){
        resetBatch();
        return;
    }

    batchPart->material->set<int>("u_useTexture", (batchMode == BatchMode2D::Textured) ? 1 : 0);
    batchPart->material->set<std::shared_ptr<Texture>>("u_texture", batchTexture);
    batchPart->mesh->upload(batchVertices, batchIndices);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    batchPart->draw(
        Math3D::Mat4(),
        uiCamera->getViewMatrix(),
        uiCamera->getProjectionMatrix()
    );

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    resetBatch();
}

// Static Helper Methods to Change the Graphics Context, or draw things like "DrawLine"
void Graphics2D::SetForegroundColor(Graphics2D& graphics, const Color& foregroundColor){
    graphics.context.foregroundColor.set(foregroundColor);
}

void Graphics2D::SetBackgroundColor(Graphics2D& graphics, const Color& backgroundColor){
    graphics.context.backgroundColor.set(backgroundColor);
}

void Graphics2D::SetStrokeWidth(Graphics2D& graphics, float width){
    graphics.context.strokeWidth.set(width);
}

void Graphics2D::SetFontSize(Graphics2D& graphics, float size){
    graphics.context.fontSize.set(size);
}

void Graphics2D::SetFont(Graphics2D& graphics, PFont font){
    if(!font) return;
    graphics.context.font.set(font);

}

void Graphics2D::DrawLine(Graphics2D& graphics, float x1, float y1, float x2, float y2){

    float dx = x2 - x1;
    float dy = y2 - y1;
    float length = std::sqrt(dx*dx + dy*dy);

    if(length < 0.1f) return;
    const float invLen = 1.0f / length;
    const float thickness = Math3D::Max(0.5f, graphics.context.strokeWidth.get());
    const float halfThickness = thickness * 0.5f;
    const float perpX = (-dy * invLen) * halfThickness;
    const float perpY = ( dx * invLen) * halfThickness;

    const Math3D::Vec4 color = graphics.context.backgroundColor.get();
    graphics.submitQuad(
        Math3D::Vec3(x1 - perpX, y1 - perpY, 0.0f),
        Math3D::Vec3(x1 + perpX, y1 + perpY, 0.0f),
        Math3D::Vec3(x2 + perpX, y2 + perpY, 0.0f),
        Math3D::Vec3(x2 - perpX, y2 - perpY, 0.0f),
        Math3D::Vec2(0, 0),
        Math3D::Vec2(0, 0),
        Math3D::Vec2(0, 0),
        Math3D::Vec2(0, 0),
        color,
        BatchMode2D::Solid
    );


    /*
    // Calculate vector properties
    float dx = x2 - x1;
    float dy = y2 - y1;
    
    // Length of the line
    float length = std::sqrt(dx*dx + dy*dy);
    if(length < Math3D::EPSILON) return;

    // Angle in radians
    float angleRad = std::atan2(dy, dx);
    // Convert to degrees for Math3D (Assuming setRotation takes degrees)
    float angleDeg = angleRad * (180.0f / Math3D::PI);

    // Calculate midpoint for position
    float midX = (x1 + x2) / 2.0f;
    float midY = (y1 + y2) / 2.0f;
    float thickness = graphics.context.strokeWidth.get();

    Math3D::Transform transform;
    transform.setPosition(Math3D::Vec3(midX, midY, 0));
    transform.setScale(Math3D::Vec3(length, thickness, 1));
    transform.setRotation(0, 0, angleDeg);
    //transform.setRotation(0, angleDeg, 0);

    // Draw using the Unit Quad (stretched to look like a line)
    graphics.drawMesh(graphics.unitQuad, transform.toMat4());*/
}


void Graphics2D::DrawRect(Graphics2D& graphics, float x, float y, float w, float h){
    // Top
    DrawLine(graphics, x, y, x + w, y);
    // Bottom
    DrawLine(graphics, x, y + h, x + w, y + h);
    // Left
    DrawLine(graphics, x, y, x, y + h);
    // Right
    DrawLine(graphics, x + w, y, x + w, y + h);
}


void Graphics2D::FillRect(Graphics2D& graphics, float x, float y, float w, float h){
    if(w == 0.0f || h == 0.0f){
        return;
    }

    const Math3D::Vec4 color = graphics.context.backgroundColor.get();
    graphics.submitQuad(
        Math3D::Vec3(x,     y,     0.0f),
        Math3D::Vec3(x + w, y,     0.0f),
        Math3D::Vec3(x + w, y + h, 0.0f),
        Math3D::Vec3(x,     y + h, 0.0f),
        Math3D::Vec2(0, 0),
        Math3D::Vec2(0, 0),
        Math3D::Vec2(0, 0),
        Math3D::Vec2(0, 0),
        color,
        BatchMode2D::Solid
    );
}


void Graphics2D::DrawEllipse(Graphics2D& graphics, float x, float y, float w, float h){
    // Since we don't have a "Ring" mesh, we approximate the ellipse using line segments.
    // Increase segments for smoother circles.
    const int segments = 32;
    
    float centerX = x + (w / 2.0f);
    float centerY = y + (h / 2.0f);
    float radiusX = w / 2.0f;
    float radiusY = h / 2.0f;

    float prevX = centerX + std::cos(0) * radiusX;
    float prevY = centerY + std::sin(0) * radiusY;

    for(int i = 1; i <= segments; i++){
        // Angle goes from 0 to 2PI
        float angle = (float)i * (2.0f * 3.14159265f) / (float)segments;
        
        float nextX = centerX + std::cos(angle) * radiusX;
        float nextY = centerY + std::sin(angle) * radiusY;

        DrawLine(graphics, prevX, prevY, nextX, nextY);

        prevX = nextX;
        prevY = nextY;
    }
}

void Graphics2D::FillEllipse(Graphics2D& graphics, float x, float y, float w, float h){
    graphics.flushBatch();
    Math3D::Transform transform;
    
    // Position at center
    transform.setPosition(Math3D::Vec3(x + (w / 2.0f), y + (h / 2.0f), 0));
    transform.setScale(Math3D::Vec3(w, h, 1));

    // Uses unitCircle (Assumes the prefab is a mesh with diameter 1.0)
    graphics.drawMesh(graphics.unitCircle, transform.toMat4());
}

void Graphics2D::DrawImage(Graphics2D& graphics, PTexture tex, float x, float y, float w, float h){ // if w or h is -1 use default texture size from file.

    if(!tex) return;
    float nW = (w <= 0) ? tex->getWidth() : w;
    float nH = (h <= 0) ? tex->getHeight() : h;
    graphics.submitQuad(
        Math3D::Vec3(x,      y,      0.0f),
        Math3D::Vec3(x + nW, y,      0.0f),
        Math3D::Vec3(x + nW, y + nH, 0.0f),
        Math3D::Vec3(x,      y + nH, 0.0f),
        Math3D::Vec2(0, 0),
        Math3D::Vec2(1, 0),
        Math3D::Vec2(1, 1),
        Math3D::Vec2(0, 1),
        Color::WHITE,
        BatchMode2D::Textured,
        tex
    );
}

void Graphics2D::DrawString(Graphics2D& graphics, std::string text, float x, float y, bool useCache){
    graphics.flushBatch();
    auto font = graphics.context.font.get();
    if(!font) return;
    font->drawText(text, Math3D::Vec2(x, y), graphics.uiCamera, graphics.context.foregroundColor.get(), useCache);
}

void Graphics2D::drawMesh(std::shared_ptr<ModelPart> part, const Math3D::Mat4& modelMatrix, PTexture tex){
    flushBatch();
    /*
    // 1. Setup the material color based on current context
    auto mat = std::dynamic_pointer_cast<MaterialDefaults::ColorMaterial>(this->colorMaterial);
    if(mat) {
        mat->Color = this->context.foregroundColor.get();
    }

    // 2. Assign material and Draw
    // Note: We use the existing UI Camera matrices
    part->material = this->colorMaterial;
    part->draw(
        modelMatrix, 
        uiCamera->getViewMatrix(),//Math3D::Mat4(), // No View Matrix
        uiCamera->getProjectionMatrix()
    );*/

    auto mat = std::dynamic_pointer_cast<MaterialDefaults::ColorMaterial>(this->colorMaterial);
    if(mat){
        mat->Color = this->context.backgroundColor.get(); // Meshes use the background color.
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    part->material = this->colorMaterial;
    part->draw(
        modelMatrix,
        uiCamera->getViewMatrix(),
        uiCamera->getProjectionMatrix()
    );

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}
