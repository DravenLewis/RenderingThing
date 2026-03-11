/**
 * @file src/Rendering/Core/FrameBuffer.h
 * @brief Declarations for FrameBuffer.
 */

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <memory>
#include <vector>
#include <cstdlib>

#include "Rendering/Textures/Texture.h"
#include "Foundation/Math/Math3D.h"
#include "Foundation/Math/Color.h"

#include "Foundation/Logging/Logbot.h"

typedef GLuint FrameBufferObject;

/// @brief Holds data for FrameBuffer.
struct FrameBuffer{
    private:
        FrameBufferObject fboID;
        int width, height;

        PTexture texturePtr;
        PTexture depthTexture;

        /// @brief Holds data for GBufferAttachment.
        struct GBufferAttachment{
            PTexture texture;
            GLenum internalFormat = GL_RGBA8;
            GLenum format = GL_RGBA;
            GLenum type = GL_UNSIGNED_BYTE;
        };

        std::vector<GBufferAttachment> gbufferAttachments;
    
    public:
        /**
         * @brief Constructs a new FrameBuffer instance.
         * @param width Dimension value.
         * @param height Dimension value.
         */
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
            // Linear filtering keeps post-process depth lookups (SSAO/DOF) stable across pixel edges.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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

        /**
         * @brief Destroys this FrameBuffer instance.
         */
        ~FrameBuffer(){
            glDeleteFramebuffers(1, &fboID);
        }

        /**
         * @brief Binds this resource.
         */
        void bind(){
            glBindFramebuffer(GL_FRAMEBUFFER, fboID);
            glViewport(0,0,width, height);
        }

        /**
         * @brief Unbinds this resource.
         */
        void unbind(){
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        /**
         * @brief Resizes internal resources.
         * @param width Dimension value.
         * @param height Dimension value.
         */
        void resize(int width, int height){
            this->width = width;
            this->height = height;

            // Resize Depth Texture
            if(depthTexture){
                glBindTexture(GL_TEXTURE_2D, depthTexture->getID());
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            if(!gbufferAttachments.empty()){
                for(auto& attachment : gbufferAttachments){
                    if(!attachment.texture) continue;
                    glBindTexture(GL_TEXTURE_2D, attachment.texture->getID());
                    glTexImage2D(
                        GL_TEXTURE_2D,
                        0,
                        attachment.internalFormat,
                        width,
                        height,
                        0,
                        attachment.format,
                        attachment.type,
                        NULL
                    );
                }
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        /**
         * @brief Attaches a texture to the framebuffer.
         * @param tex Value for tex.
         */
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

        /**
         * @brief Checks whether validate.
         * @return True when the operation succeeds; otherwise false.
         */
        bool validate(){
            bind();
            bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
            unbind();
            return complete;
        }

        /**
         * @brief Clears the current state.
         * @param clearColor Color value.
         */
        void clear(Color clearColor = Color::BLACK){
            glClearColor(
                clearColor.getRed(),
                clearColor.getGreen(),
                clearColor.getBlue(),
                clearColor.getAlpha()
            );

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        /**
         * @brief Creates a new object.
         * @param width Dimension value.
         * @param height Dimension value.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<FrameBuffer> Create(int width, int height){
            auto fbuffer = std::make_shared<FrameBuffer>(width, height);
            return fbuffer;
        }

        /**
         * @brief Creates g buffer.
         * @param width Dimension value.
         * @param height Dimension value.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<FrameBuffer> CreateGBuffer(int width, int height){
            auto fbuffer = std::make_shared<FrameBuffer>(width, height);

            fbuffer->gbufferAttachments.clear();
            fbuffer->gbufferAttachments.reserve(3);

            auto createAttachment = [&](GLenum internalFormat, GLenum format, GLenum type) -> PTexture {
                GLuint texId = 0;
                glGenTextures(1, &texId);
                glBindTexture(GL_TEXTURE_2D, texId);
                glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, NULL);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindTexture(GL_TEXTURE_2D, 0);
                return Texture::CreateFromExisting(texId, width, height, true);
            };

            fbuffer->bind();

            {
                GBufferAttachment albedo;
                albedo.internalFormat = GL_RGBA8;
                albedo.format = GL_RGBA;
                albedo.type = GL_UNSIGNED_BYTE;
                albedo.texture = createAttachment(albedo.internalFormat, albedo.format, albedo.type);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, albedo.texture->getID(), 0);
                fbuffer->gbufferAttachments.push_back(albedo);
            }

            {
                GBufferAttachment normal;
                normal.internalFormat = GL_RGBA16F;
                normal.format = GL_RGBA;
                normal.type = GL_FLOAT;
                normal.texture = createAttachment(normal.internalFormat, normal.format, normal.type);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, normal.texture->getID(), 0);
                fbuffer->gbufferAttachments.push_back(normal);
            }

            {
                GBufferAttachment position;
                // Large receivers (e.g. 200x200 ground planes) need full precision to avoid deferred-light banding.
                position.internalFormat = GL_RGBA32F;
                position.format = GL_RGBA;
                position.type = GL_FLOAT;
                position.texture = createAttachment(position.internalFormat, position.format, position.type);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, position.texture->getID(), 0);
                fbuffer->gbufferAttachments.push_back(position);
            }

            GLenum drawBuffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
            glDrawBuffers(3, drawBuffers);
            glReadBuffer(GL_COLOR_ATTACHMENT0);

            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if(status != GL_FRAMEBUFFER_COMPLETE){
                std::string error;
                switch(status){
                    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:         error = "Incomplete Attachment"; break;
                    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: error = "Missing Attachment"; break;
                    case GL_FRAMEBUFFER_UNSUPPORTED:                   error = "Unsupported Format"; break;
                    default:                                           error = "Unknown Error"; break;
                }
                LogBot.Log(LOG_ERRO, "[FrameBuffer] GBuffer Error: %s (Code: %u)", error.c_str(), status);
            }

            fbuffer->unbind();

            return fbuffer;
        }

        /**
         * @brief Returns the texture.
         * @return Result of this operation.
         */
        PTexture getTexture(){
            return texturePtr;
        }

        /**
         * @brief Returns the depth texture.
         * @return Result of this operation.
         */
        PTexture getDepthTexture(){
            return depthTexture;
        }

        /**
         * @brief Returns the g buffer texture.
         * @param index Identifier or index value.
         * @return Result of this operation.
         */
        PTexture getGBufferTexture(size_t index) const{
            if(index >= gbufferAttachments.size()){
                return nullptr;
            }
            return gbufferAttachments[index].texture;
        }

        /**
         * @brief Returns the g buffer count.
         * @return Computed numeric result.
         */
        size_t getGBufferCount() const{
            return gbufferAttachments.size();
        }

        /**
         * @brief Returns the id.
         * @return Result of this operation.
         */
        FrameBufferObject getID() const { return fboID; }
        int getWidth() const { return width; }
        int getHeight() const { return height; }
};

/// @brief Holds data for FrameBufferChain.
struct FrameBufferChain{
    private:
        /**
         * @brief Constructs a new FrameBufferChain instance.
         */
        FrameBufferChain() = delete;
        int size = 0;
        std::vector<std::shared_ptr<FrameBuffer>> frameBufferPtrArray;
    public:
        /**
         * @brief Constructs a new FrameBufferChain instance.
         * @param s Value for s.
         */
        FrameBufferChain(int s) : size(s) {
            frameBufferPtrArray.resize(s);
        };

        /**
         * @brief Returns the at index.
         * @param index Identifier or index value.
         * @return Pointer to the resulting object.
         */
        std::shared_ptr<FrameBuffer> getAtIndex(int index){
            if(index < 0 || index >= size) return nullptr;
            return frameBufferPtrArray[index];
        }

        /**
         * @brief Sets the at index.
         * @param index Identifier or index value.
         * @param fbPtr Pointer to fb.
         */
        void setAtIndex(int index, std::shared_ptr<FrameBuffer> fbPtr){
            if(index < 0 || index >= size) return;
            frameBufferPtrArray[index] = fbPtr;
        }

        /**
         * @brief Returns the size.
         * @return Computed numeric result.
         */
        int getSize(){
            return size;
        }

        /**
         * @brief Resizes internal resources.
         * @param newSize Number of elements or bytes.
         */
        void resize(int newSize){
            if(newSize <= 0) return;
            frameBufferPtrArray.resize(newSize);
            size = newSize;
        }

        /**
         * @brief Resizes backing GPU buffers.
         * @param width Dimension value.
         * @param height Dimension value.
         */
        void resizeBuffers(int width, int height){
            for(auto buffer : frameBufferPtrArray){
                if(buffer) {
                    buffer->resize(width,height);
                }
            }
        }
};

/// @brief Holds data for TrippleBuffer.
struct TrippleBuffer{
    private:
        FrameBufferChain chain = FrameBufferChain(3);
        int width, height;
        /**
         * @brief Constructs a new TrippleBuffer instance.
         */
        TrippleBuffer() = delete;
    public:

        /**
         * @brief Constructs a new TrippleBuffer instance.
         * @param width Dimension value.
         * @param height Dimension value.
         */
        TrippleBuffer(int width, int height) : width(width), height(height) {
            chain.setAtIndex(0, FrameBuffer::Create(width, height));
            chain.setAtIndex(1, FrameBuffer::Create(width, height));
            chain.setAtIndex(2, FrameBuffer::Create(width, height));
        }

        /**
         * @brief Returns the front.
         * @return Pointer to the resulting object.
         */
        std::shared_ptr<FrameBuffer> getFront(){
            return chain.getAtIndex(0);
        }
        
        /**
         * @brief Returns the middle.
         * @return Pointer to the resulting object.
         */
        std::shared_ptr<FrameBuffer> getMiddle(){
            return chain.getAtIndex(1);
        }

        /**
         * @brief Returns the back.
         * @return Pointer to the resulting object.
         */
        std::shared_ptr<FrameBuffer> getBack(){
            return chain.getAtIndex(2);
        }


        // Helper functions for understanding the order.

        std::shared_ptr<FrameBuffer> getDisplayBuffer(){
            return getFront();
        }

        /**
         * @brief Returns the edit buffer.
         * @return Pointer to the resulting object.
         */
        std::shared_ptr<FrameBuffer> getEditBuffer(){
            return getMiddle();
        }

        /**
         * @brief Returns the draw buffer.
         * @return Pointer to the resulting object.
         */
        std::shared_ptr<FrameBuffer> getDrawBuffer(){
            return getBack();
        }

        // ===================================================

        void setFront(std::shared_ptr<FrameBuffer> framePtr){
            chain.setAtIndex(0,framePtr);
        }

        /**
         * @brief Sets the middle.
         * @param framePtr Pointer to frame.
         */
        void setMiddle(std::shared_ptr<FrameBuffer> framePtr){
            chain.setAtIndex(1,framePtr);
        }

        /**
         * @brief Sets the back.
         * @param framePtr Pointer to frame.
         */
        void setBack(std::shared_ptr<FrameBuffer> framePtr){
            chain.setAtIndex(2,framePtr);
        }

        /**
         * @brief Swaps buffers.
         */
        void swapBuffers(){
            auto frontBuffer = getFront();
            auto middleBuffer = getMiddle();
            auto backBuffer = getBack();

            setFront(middleBuffer);
            setMiddle(backBuffer);
            setBack(frontBuffer);
        }

        /**
         * @brief Resizes backing GPU buffers.
         * @param width Dimension value.
         * @param height Dimension value.
         */
        void resizeBuffers(int width, int height){
            chain.resizeBuffers(width, height);
        }
};

typedef std::shared_ptr<FrameBuffer> PFrameBuffer;

#endif // FRAMEBUFFER_H
