
#include "Screen.h"
#include "ShadowRenderer.h"

PCamera Screen::CurrentCamera = nullptr;

Screen::Screen(int width, int height){
    this->buffer = std::make_unique<TrippleBuffer>(width, height);

    initScreenShader();
    initScreenGeom();

    resize(width, height);
}

Screen::~Screen(){
    // Cleanup for later...
}

void Screen::initScreenGeom(){
    // 1. Create the ModelPart manually so we can control the vertices (XY Plane)
    // We pass 'nullptr' for the material so the draw call doesn't override our shader.
    auto factory = ModelPartFactory::Create(nullptr); 
    int v1, v2, v3, v4;

    factory
        // Bottom Left
        .addVertex(Vertex::Build(Math3D::Vec3(-1.0f, -1.0f, 0.0f)).UV(0, 0), &v1)
        // Bottom Right
        .addVertex(Vertex::Build(Math3D::Vec3( 1.0f, -1.0f, 0.0f)).UV(1, 0), &v2)
        // Top Right
        .addVertex(Vertex::Build(Math3D::Vec3( 1.0f,  1.0f, 0.0f)).UV(1, 1), &v3)
        // Top Left
        .addVertex(Vertex::Build(Math3D::Vec3(-1.0f,  1.0f, 0.0f)).UV(0, 1), &v4)
        
        .defineFace(v1, v2, v3, v4);

    this->screenQuad = factory.assemble();
}

void Screen::initScreenShader(){
    this->screenShader = std::make_shared<ShaderProgram>();
    this->screenShader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
    this->screenShader->setFragmentShader(Graphics::ShaderDefaults::SCREEN_FRAG_SRC);

    Shader shader = this->screenShader->compile();
    if(shader == 0){
        LogBot.Log(LOG_ERRO, "Failed to compile / link internal Screen shader: \n%s", this->screenShader->getLog().c_str());
    }
}

void Screen::processRenderPipeline(){
    /*auto drawBuffer = buffer->getDrawBuffer();
    auto editBuffer = buffer->getEditBuffer();

    // Prepare the middle frame for population with data from 3D world.
    editBuffer->bind();
    editBuffer->clear();
    glDisable(GL_DEPTH_TEST);
    
    if(screenShader && screenQuad){
        screenShader->bind();

        Uniform<PTexture> u_tex(drawBuffer->getTexture());
        screenShader->setUniform("screenTexture", u_tex);


    }*/

    // 1. Setup Initial Pointers
    // 'readSource' starts as the Back Buffer (where the 3D scene is)
    // 'writeTarget' starts as the Middle Buffer (our scratchpad)
    std::shared_ptr<FrameBuffer> readSource = buffer->getDrawBuffer(); 
    std::shared_ptr<FrameBuffer> writeTarget = buffer->getEditBuffer();

    bool performedAnyEffects = false;

    // 2. Run the Effect Stack (Ping-Pong Logic)
    // If the list is empty, this loop is skipped entirely.
    for (auto& effect : effects) {
        if (!effect) continue;

        performedAnyEffects = true;

        auto originalDepth = buffer->getDrawBuffer()->getDepthTexture();

        // Apply the effect: Read from Source -> Write to Target
        effect->apply(readSource->getTexture(), originalDepth, writeTarget, screenQuad);

        // Swap the pointers locally!
        // The 'Target' now holds the latest image, so it becomes the 'Source' for the next pass.
        // The old 'Source' is now recycled as the new 'Target'.
        std::swap(readSource, writeTarget);
    }

    // 3. Finalization
    // We need to ensure the final image ends up in the specific "Middle" buffer 
    // so that buffer->swapBuffers() works correctly.
    
    // The valid image is currently inside 'readSource' (because we swapped at end of loop).
    // The required destination is 'buffer->getEditBuffer()' (The Middle Buffer).
    
    // We need a final copy pass if:
    // A) No effects ran (Raw Scene is in Back, needs to go to Middle)
    // B) The Ping-Pong loop ended with the image in 'Back' instead of 'Middle'
    if (!performedAnyEffects || readSource != buffer->getEditBuffer()) {
        
        auto finalDestination = buffer->getEditBuffer();

        // Bind the definitive Middle buffer
        finalDestination->bind();
        finalDestination->clear(Color::CLEAR); // WAS BLACK
        glDisable(GL_DEPTH_TEST);

        if (screenShader && screenQuad) {
            screenShader->bind();

            // Read from wherever the valid data ended up
            Uniform<PTexture> u_tex(readSource->getTexture());
            screenShader->setUniform("screenTexture", u_tex);

            glDisable(GL_BLEND); // Disable Blending.

            // Draw the Quad (The "Brush Stroke")
            static const Math3D::Mat4 IDENTITY;
            screenQuad->draw(IDENTITY, IDENTITY, IDENTITY);
        }

        finalDestination->unbind();
    }

    // 4. Rotate the TrippleBuffer
    // Middle (which now DEFINITELY holds the final image) becomes Front.
    // Front becomes Back.
    // Back becomes Middle.
    buffer->swapBuffers();
}

void Screen::drawToWindow(RenderWindow* window, bool clearWindow){
    if(window){
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0,0, window->getWindowWidth(),window->getWindowHeight());

        if(clearWindow){
            clear(this->getClearColor());
        }

        glDisable(GL_DEPTH_TEST); 
        glDisable(GL_CULL_FACE);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


        if(screenShader && screenQuad){
            screenShader->bind();

            Uniform<PTexture> texUniform(buffer->getDisplayBuffer()->getTexture());
            screenShader->setUniform("screenTexture", texUniform);

            screenQuad->draw(Math3D::Mat4(),Math3D::Mat4(),Math3D::Mat4());
        }

        //glEnable(GL_DEPTH_TEST);
    }
}

void Screen::clear(Color c){
    glClearColor(c.x,c.y,c.z,c.w);
    glClear(GL_COLOR_BUFFER_BIT);
}

PTexture Screen::getDisplayTexture(){
    return this->buffer->getDisplayBuffer()->getTexture();
}

void Screen::resize(int w, int h){

    if(buffer){

        this->buffer->resizeBuffers(w,h);
        this->width = w;
        this->height = h;

        buffer->getBack()->attachTexture(Texture::CreateEmpty(w,h));
        buffer->getMiddle()->attachTexture(Texture::CreateEmpty(w,h));
        buffer->getFront()->attachTexture(Texture::CreateEmpty(w,h));
    }

    if(camera){
        camera->resize(w,h);
    }

}

int Screen::getWidth(){
    return this->width;
}

int Screen::getHeight(){
    return this->height;
}

void Screen::bind(bool clear){

    if(isBound()) return; // already bound this state is already set.

    auto back = buffer->getDrawBuffer();
    back->bind();

    // Safety: Force masks on so Clear actually works
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);

    if(clear) { back->clear(this->getClearColor()); }

    glEnable(GL_DEPTH_TEST);
    this->bound = true;

    if(this->camera){
        ShadowRenderer::BeginFrame(this->camera);
    }
}

void Screen::unbind(){

    if(!isBound()) return; // Normally a return wouldnt be needed here, but we need it for tracking the bound state.

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    this->bound = false;

    processRenderPipeline(); // Process the pipeline;
}

void Screen::addEffect(Graphics::PostProcessing::PPostProcessingEffect effect){
    this->effects.push_back(effect);
}

void Screen::clearEffects(){
    this->effects.clear();
}

void Screen::setCamera(PCamera cam, bool makeCurrent) {
    this->camera = cam;
    if(makeCurrent){
        this->makeCameraCurrent();
    }
}

void Screen::MakeCameraCurrent(PCamera camera){
    Screen::CurrentCamera = camera;
}

PCamera Screen::GetCurrentCamera(){
    return Screen::CurrentCamera;
}

void Screen::makeCameraCurrent(){
    if(this->camera){
        Screen::MakeCameraCurrent(this->camera);
    }
}
