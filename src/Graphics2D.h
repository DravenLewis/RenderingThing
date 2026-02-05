#ifndef GRAPHICS2D_H
#define GRAPHICS2D_H

#include "Math.h"
#include "Screen.h"
#include "Camera.h"
#include "Texture.h"
#include "ModelPartPrefabs.h" // Also includes ModelPart.h
#include "MaterialDefaults.h"
#include "ValueContainer.h"
#include "Color.h"
#include "Font.h"

// We Need FreeTypeFont and Asset for the default font to work.
#include "FreeTypeFont.h"
#include "Asset.h"

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

class Graphics2D{
    private:

        // Graphics Context.
        GraphicsContext context;

        PScreen target;
        PCamera uiCamera;

        // Reusable Items. Are Null just used so we can modify things.
        std::shared_ptr<ModelPart> unitQuad = nullptr; 
        std::shared_ptr<ModelPart> unitCircle = nullptr;
        PMaterial colorMaterial = nullptr;
        PMaterial imageMaterial = nullptr;


        // Internal Helpers.
        void drawMesh(std::shared_ptr<ModelPart> part, const Math3D::Mat4& modelMatrix, PTexture tex = nullptr);
    
    public:
        Graphics2D(PScreen screenContext);
        
        // lifecycle
        void resize(int w, int h);
        void begin();
        void end();

        GraphicsContext& getContext();

        // Static Helper Methods to Change the Graphics Context, or draw things like "DrawLine"

        // Setters
        static void SetForegroundColor(Graphics2D& graphics, const Color& foregroundColor = Color::WHITE);
        static void SetBackgroundColor(Graphics2D& graphics, const Color& backgroundColor = Color::BLACK);
        static void SetStrokeWidth(Graphics2D& graphics, float width = 1.0f);
        static void SetFontSize(Graphics2D& graphics, float size = 1.0f);
        static void SetFont(Graphics2D& graphics, PFont font);

        static void DrawLine(Graphics2D& graphics, float x1, float y1, float x2, float y2);
        static void DrawRect(Graphics2D& graphics, float x, float y, float w, float h);
        static void FillRect(Graphics2D& graphics, float x, float y, float w, float h);
        static void DrawEllipse(Graphics2D& graphics, float x, float y, float w, float h);
        static void FillEllipse(Graphics2D& graphics, float x, float y, float w, float h);

        static void DrawImage(Graphics2D& graphics, PTexture tex, float x, float y, float w = -1, float h = -1); // if w or h is -1 use default texture size from file.

        static void DrawString(Graphics2D& graphics, std::string text, float x, float y);
};

#endif//GRAPHICS2D_H