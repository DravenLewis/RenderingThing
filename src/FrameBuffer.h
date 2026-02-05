#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <memory>
#include <vector>
#include <cstdlib>

#include "Texture.h"
#include "Math.h"
#include "Color.h"

#include "Logbot.h"

typedef GLuint FrameBufferObject;

struct FrameBuffer{
    private:
        FrameBufferObject fboID;
        int width, height;

        PTexture texturePtr;
        PTexture depthTexture;
    
    public:
        FrameBuffer(int width, int height) : width(width), height(height) {
            glGenFramebuffers(1, &fboID);
            
            // 1. Create the Depth Texture
            // We do this manually here (or you could add a helper in Texture.h)
            depthTexture = std::make_shared<Texture>();
            // Note: We don't set width/height on the Texture object here manually 
            // unless your Texture class requires it.

            GLuint tid;
            glGenTextures(1, &tid);
            depthTexture->getID() = tid;

            // DEBUG CHECK
            if(depthTexture->getID() == 0) {
                LogBot.Log(LOG_FATL, "[FrameBuffer] CRITICAL: glGenTextures failed for Depth Texture!");
            }

            glBindTexture(GL_TEXTURE_2D, depthTexture->getID());

            // GL_DEPTH_COMPONENT24 is standard for precision
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
            
            // Essential Texture Parameters for Depth
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); 
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            
            // Border color for depth (white = far away) prevents artifacts at screen edges
            float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

            // 2. Attach Depth Texture to FBO
            bind();
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture->getID(), 0);
            
            // Note: We are currently NOT complete because we have no Color buffer yet.
            // Screen class attaches color later via attachTexture().
            // If you want to use this for Shadow Mapping (Depth Only), you need to tell GL to draw no color:
            // glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);

            // Crucial: Tell OpenGL we aren't drawing to color YET
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            
            unbind();
        }

        ~FrameBuffer(){
            glDeleteFramebuffers(1, &fboID);
        }

        void bind(){
            glBindFramebuffer(GL_FRAMEBUFFER, fboID);
            glViewport(0,0,width, height);
        }

        void unbind(){
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void resize(int width, int height){
            this->width = width;
            this->height = height;

            // Resize Depth Texture
            if(depthTexture){
                glBindTexture(GL_TEXTURE_2D, depthTexture->getID());
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        void attachTexture(PTexture tex){

            this->texturePtr = tex;

            bind();
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->getID(), 0);

            GLenum drawBuffers[1] = {GL_COLOR_ATTACHMENT0};
            glDrawBuffers(1, drawBuffers);
            glReadBuffer(GL_COLOR_ATTACHMENT0);

            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                std::string error;
                switch (status) {
                    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:         error = "Incomplete Attachment"; break;
                    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: error = "Missing Attachment"; break;
                    case GL_FRAMEBUFFER_UNSUPPORTED:                   error = "Unsupported Format"; break;
                    default:                                           error = "Unknown Error"; break;
                }
                LogBot.Log(LOG_ERRO, "[FrameBuffer] FBO Error: %s (Code: %u)", error.c_str(), status);
            }

            unbind();
        }

        bool validate(){
            bind();
            bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
            unbind();
            return complete;
        }

        void clear(Color clearColor = Color::BLACK){
            glClearColor(
                clearColor.getRed(),
                clearColor.getGreen(),
                clearColor.getBlue(),
                clearColor.getAlpha()
            );

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        static std::shared_ptr<FrameBuffer> Create(int width, int height){
            auto fbuffer = std::make_shared<FrameBuffer>(width, height);
            return fbuffer;
        }

        PTexture getTexture(){
            return texturePtr;
        }

        PTexture getDepthTexture(){
            return depthTexture;
        }
};

struct FrameBufferChain{
    private:
        FrameBufferChain() = delete;
        int size = 0;
        std::vector<std::shared_ptr<FrameBuffer>> frameBufferPtrArray;
    public:
        FrameBufferChain(int s) : size(s) {
            frameBufferPtrArray.resize(s);
        };

        std::shared_ptr<FrameBuffer> getAtIndex(int index){
            if(index < 0 || index >= size) return nullptr;
            return frameBufferPtrArray[index];
        }

        void setAtIndex(int index, std::shared_ptr<FrameBuffer> fbPtr){
            if(index < 0 || index >= size) return;
            frameBufferPtrArray[index] = fbPtr;
        }

        int getSize(){
            return size;
        }

        void resize(int newSize){
            if(newSize <= 0) return;
            frameBufferPtrArray.resize(newSize);
            size = newSize;
        }

        void resizeBuffers(int width, int height){
            for(auto buffer : frameBufferPtrArray){
                if(buffer) {
                    buffer->resize(width,height);
                }
            }
        }
};

struct TrippleBuffer{
    private:
        FrameBufferChain chain = FrameBufferChain(3);
        int width, height;
        TrippleBuffer() = delete;
    public:

        TrippleBuffer(int width, int height) : width(width), height(height) {
            chain.setAtIndex(0, FrameBuffer::Create(width, height));
            chain.setAtIndex(1, FrameBuffer::Create(width, height));
            chain.setAtIndex(2, FrameBuffer::Create(width, height));
        }

        std::shared_ptr<FrameBuffer> getFront(){
            return chain.getAtIndex(0);
        }
        
        std::shared_ptr<FrameBuffer> getMiddle(){
            return chain.getAtIndex(1);
        }

        std::shared_ptr<FrameBuffer> getBack(){
            return chain.getAtIndex(2);
        }


        // Helper functions for understanding the order.

        std::shared_ptr<FrameBuffer> getDisplayBuffer(){
            return getFront();
        }

        std::shared_ptr<FrameBuffer> getEditBuffer(){
            return getMiddle();
        }

        std::shared_ptr<FrameBuffer> getDrawBuffer(){
            return getBack();
        }

        // ===================================================

        void setFront(std::shared_ptr<FrameBuffer> framePtr){
            chain.setAtIndex(0,framePtr);
        }

        void setMiddle(std::shared_ptr<FrameBuffer> framePtr){
            chain.setAtIndex(1,framePtr);
        }

        void setBack(std::shared_ptr<FrameBuffer> framePtr){
            chain.setAtIndex(2,framePtr);
        }

        void swapBuffers(){
            auto frontBuffer = getFront();
            auto middleBuffer = getMiddle();
            auto backBuffer = getBack();

            setFront(middleBuffer);
            setMiddle(backBuffer);
            setBack(frontBuffer);
        }

        void resizeBuffers(int width, int height){
            chain.resizeBuffers(width, height);
        }
};

typedef std::shared_ptr<FrameBuffer> PFrameBuffer;

#endif // FRAMEBUFFER_H