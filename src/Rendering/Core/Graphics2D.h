/**
 * @file src/Rendering/Core/Graphics2D.h
 * @brief Declarations for Graphics2D.
 */

#ifndef GRAPHICS2D_H
#define GRAPHICS2D_H

#include "Foundation/Math/Math3D.h"
#include "Rendering/Core/Screen.h"
#include "Scene/Camera.h"
#include "Rendering/Textures/Texture.h"
#include "Rendering/Geometry/ModelPartPrefabs.h" // Also includes ModelPart.h
#include "Rendering/Materials/MaterialDefaults.h"
#include "Foundation/Util/ValueContainer.h"
#include "Foundation/Math/Color.h"
#include "Rendering/Fonts/Font.h"

// We Need FreeTypeFont and Asset for the default font to work.
#include "Rendering/Fonts/FreeTypeFont.h"
#include "Assets/Core/Asset.h"

/// @brief Holds data for GraphicsContext.
struct GraphicsContext{
    private:
        ValueContainer<PAsset> defaultFontLocation{AssetManager::Instance.getOrLoad("@assets/fonts/arial.ttf")};
    public:
        ValueContainer<Color> foregroundColor{Color::WHITE};
        ValueContainer<Color> backgroundColor{Color::BLACK};
        ValueContainer<float> strokeWidth{1.0f};
        ValueContainer<PFont> font{Font::Create<FreeTypeFont>(defaultFontLocation.get(), 18.0f)};
        ValueContainer<int> fontSize{18};
};

/// @brief Represents the Graphics2D type.
class Graphics2D{
    private:
        /// @brief Enumerates values for BatchMode2D.
        enum class BatchMode2D {
            None = 0,
            Solid,
            Textured
        };

        // Graphics Context.
        GraphicsContext context;

        PScreen target;
        PCamera uiCamera;

        // Reusable Items. Are Null just used so we can modify things.
        std::shared_ptr<ModelPart> unitQuad = nullptr; 
        std::shared_ptr<ModelPart> unitCircle = nullptr;
        PMaterial colorMaterial = nullptr;
        PMaterial imageMaterial = nullptr;
        std::shared_ptr<ModelPart> batchPart = nullptr;
        PMaterial batchMaterial = nullptr;
        std::vector<Vertex> batchVertices;
        std::vector<uint32_t> batchIndices;
        BatchMode2D batchMode = BatchMode2D::None;
        PTexture batchTexture = nullptr;
        size_t maxBatchQuads = 4096;
        std::vector<PFont> queuedFonts;


        // Internal Helpers.
        void drawMesh(std::shared_ptr<ModelPart> part, const Math3D::Mat4& modelMatrix, PTexture tex = nullptr);
        /**
         * @brief Ensures batch resources.
         */
        void ensureBatchResources();
        /**
         * @brief Resets batch.
         */
        void resetBatch();
        /**
         * @brief Flushes queued 2D draw batches.
         */
        void flushBatch();
        /**
         * @brief Queues a font for end-of-frame flush.
         * @param font Value for font.
         */
        void queueFontForFlush(PFont font);
        /**
         * @brief Flushes queued font draw calls.
         */
        void flushQueuedFonts();
        /**
         * @brief Begins batch.
         * @param mode Mode or type selector.
         * @param texture Value for texture.
         */
        void beginBatch(BatchMode2D mode, PTexture texture = nullptr);
        /**
         * @brief Submits a quad to the active batch.
         * @param p0 Value for p 0.
         * @param p1 Value for p 1.
         * @param p2 Value for p 2.
         * @param p3 Value for p 3.
         * @param uv0 Value for uv 0.
         * @param uv1 Value for uv 1.
         * @param uv2 Value for uv 2.
         * @param uv3 Value for uv 3.
         * @param color Color value.
         * @param mode Mode or type selector.
         * @param texture Value for texture.
         */
        void submitQuad(
            const Math3D::Vec3& p0, const Math3D::Vec3& p1, const Math3D::Vec3& p2, const Math3D::Vec3& p3,
            const Math3D::Vec2& uv0, const Math3D::Vec2& uv1, const Math3D::Vec2& uv2, const Math3D::Vec2& uv3,
            const Math3D::Vec4& color,
            BatchMode2D mode,
            PTexture texture = nullptr
        );
    
    public:
        /**
         * @brief Constructs a new Graphics2D instance.
         * @param screenContext Value for screen context.
         */
        Graphics2D(PScreen screenContext);
        
        // lifecycle
        void resize(int w, int h);
        /**
         * @brief Begins a 2D drawing batch.
         */
        void begin();
        /**
         * @brief Ends the current 2D drawing batch.
         */
        void end();

        /**
         * @brief Returns the context.
         * @return Reference to the resulting value.
         */
        GraphicsContext& getContext();

        // Static Helper Methods to Change the Graphics Context, or draw things like "DrawLine"

        // Setters
        static void SetForegroundColor(Graphics2D& graphics, const Color& foregroundColor = Color::WHITE);
        /**
         * @brief Sets the background color.
         * @param graphics Value for graphics.
         * @param backgroundColor Color value.
         */
        static void SetBackgroundColor(Graphics2D& graphics, const Color& backgroundColor = Color::BLACK);
        /**
         * @brief Sets the stroke width.
         * @param graphics Value for graphics.
         * @param width Dimension value.
         */
        static void SetStrokeWidth(Graphics2D& graphics, float width = 1.0f);
        /**
         * @brief Sets the font size.
         * @param graphics Value for graphics.
         * @param size Number of elements or bytes.
         */
        static void SetFontSize(Graphics2D& graphics, float size = 1.0f);
        /**
         * @brief Sets the font.
         * @param graphics Value for graphics.
         * @param font Value for font.
         */
        static void SetFont(Graphics2D& graphics, PFont font);

        /**
         * @brief Draws line.
         * @param graphics Value for graphics.
         * @param x1 Value for x 1.
         * @param y1 Value for y 1.
         * @param x2 Value for x 2.
         * @param y2 Value for y 2.
         */
        static void DrawLine(Graphics2D& graphics, float x1, float y1, float x2, float y2);
        /**
         * @brief Draws rect.
         * @param graphics Value for graphics.
         * @param x Spatial value used by this operation.
         * @param y Spatial value used by this operation.
         * @param w Value for w.
         * @param h Value for h.
         */
        static void DrawRect(Graphics2D& graphics, float x, float y, float w, float h);
        /**
         * @brief Fills a rectangle.
         * @param graphics Value for graphics.
         * @param x Spatial value used by this operation.
         * @param y Spatial value used by this operation.
         * @param w Value for w.
         * @param h Value for h.
         */
        static void FillRect(Graphics2D& graphics, float x, float y, float w, float h);
        /**
         * @brief Fills a triangle.
         * @param graphics Value for graphics.
         * @param x1 Value for x 1.
         * @param y1 Value for y 1.
         * @param x2 Value for x 2.
         * @param y2 Value for y 2.
         * @param x3 Value for x 3.
         * @param y3 Value for y 3.
         */
        static void FillTriangle(Graphics2D& graphics, float x1, float y1, float x2, float y2, float x3, float y3);
        /**
         * @brief Draws ellipse.
         * @param graphics Value for graphics.
         * @param x Spatial value used by this operation.
         * @param y Spatial value used by this operation.
         * @param w Value for w.
         * @param h Value for h.
         */
        static void DrawEllipse(Graphics2D& graphics, float x, float y, float w, float h);
        /**
         * @brief Fills an ellipse.
         * @param graphics Value for graphics.
         * @param x Spatial value used by this operation.
         * @param y Spatial value used by this operation.
         * @param w Value for w.
         * @param h Value for h.
         */
        static void FillEllipse(Graphics2D& graphics, float x, float y, float w, float h);

        /**
         * @brief Draws image.
         * @param graphics Value for graphics.
         * @param tex Value for tex.
         * @param x Spatial value used by this operation.
         * @param y Spatial value used by this operation.
         * @param w Value for w.
         * @param h Value for h.
         */
        static void DrawImage(Graphics2D& graphics, PTexture tex, float x, float y, float w = -1, float h = -1); // if w or h is -1 use default texture size from file.

        /**
         * @brief Draws string.
         * @param graphics Value for graphics.
         * @param text Value for text.
         * @param x Spatial value used by this operation.
         * @param y Spatial value used by this operation.
         * @param useCache Flag controlling use cache.
         */
        static void DrawString(Graphics2D& graphics, std::string text, float x, float y, bool useCache = true);
};

#endif//GRAPHICS2D_H
