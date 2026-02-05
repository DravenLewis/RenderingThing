#include "Graphics2D.h"

Graphics2D::Graphics2D(PScreen screenContext){
    this->target = screenContext;
    this->uiCamera = screenContext->getCamera();

    this->imageMaterial = MaterialDefaults::ImageMaterial::Create(Texture::CreateEmpty(1,1));
    this->colorMaterial = MaterialDefaults::ColorMaterial::Create(Color::WHITE);

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
    this->target->bind();
}

void Graphics2D::end(){
    this->target->unbind();
}

GraphicsContext& Graphics2D::getContext(){
    return this->context;
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

    float angleRad = std::atan2(dy, dx);
    float angleDeg = angleRad * (180.0f / 3.14159265f);

    // 3. Calculate Center Position
    float midX = (x1 + x2) / 2.0f;
    float midY = (y1 + y2) / 2.0f;

    Math3D::Transform transform;
    // Position at x1, y1 (the start)
    transform.setPosition(Math3D::Vec3(midX, midY, 0));
    // Scale it: X is the length, Y is thickness
    transform.setScale(Math3D::Vec3(length, graphics.context.strokeWidth.get(), 1.0f));
    // Rotate it
    transform.setRotation(0, 0, angleDeg);

    // If your mesh is centered (like the CirclePlane or standard Plane), 
    // we need to offset it so it rotates around the START of the line.
    // This is likely why the line is "jumping" or invisible.
    
    graphics.drawMesh(graphics.unitQuad, transform.toMat4());


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
    Math3D::Transform transform;
    
    // Move origin to center of the rectangle (since the quad is centered at 0,0)
    transform.setPosition(Math3D::Vec3(x + (w / 2.0f), y + (h / 2.0f), 0));
    transform.setScale(Math3D::Vec3(w, h, 1));

    graphics.drawMesh(graphics.unitQuad, transform.toMat4());
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

    auto mat = std::dynamic_pointer_cast<MaterialDefaults::ImageMaterial>(graphics.imageMaterial);
    if(!mat) return; 

    mat->Tex = tex;

    Math3D::Transform transform;
    transform.setPosition(Math3D::Vec3(x + (nW / 2), y + (nH / 2), 0));
    transform.setScale(Math3D::Vec3(nW, nH, 1));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    graphics.unitQuad->material = mat;
    graphics.unitQuad->draw(
        transform.toMat4(),
        graphics.uiCamera->getViewMatrix(),
        graphics.uiCamera->getProjectionMatrix()
    );

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void Graphics2D::DrawString(Graphics2D& graphics, std::string text, float x, float y){
    auto font = graphics.context.font.get();
    if(!font) return;
    font->drawText(text, Math3D::Vec2(x, y), graphics.uiCamera, graphics.context.foregroundColor.get(), false); // Disable Caching
}

void Graphics2D::drawMesh(std::shared_ptr<ModelPart> part, const Math3D::Mat4& modelMatrix, PTexture tex){
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