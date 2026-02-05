#ifndef MESH_H
#define MESH_H

#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <cstdint>
#include <vector>
#include <algorithm>

#include "Math.h"
#include "Drawable.h"

typedef unsigned int VertexBufferObject, VertexArrayObject, ElementBufferObject;

struct Vertex{
    Math3D::Vec3 Position;
    Math3D::Vec3 Normal;
    Math3D::Vec2 TexCoords;
    Math3D::Vec4 Color;

    static const int VERTEX_DATA_WIDTH = 12;

    Vertex& Pos(const Math3D::Vec3& pos){
        this->Position = pos;
        return *this;
    }

    Vertex& Pos(float x = 0, float y = 0, float z = 0){
        return Pos(Math3D::Vec3(x,y,z));
    }

    Vertex& Norm(const Math3D::Vec3& nml){
        this->Normal = nml;
        return *this;
    }

    Vertex& Norm(float x = 0, float y = 0, float z = 0){
        return Norm(Math3D::Vec3(x,y,z));
    }

    Vertex& UV(const Math3D::Vec2& pos){
        this->TexCoords = pos;
        return *this;
    }

    Vertex& UV(float u = 0, float v = 0){
        return UV(Math3D::Vec2(u,v));
    }

    Vertex& Col(const Math3D::Vec4& col){
        this->Color = col;
        return *this;
    }

    Vertex& Col(float r = 0, float g = 0, float b = 0, float a = 1){
        return Col(Math3D::Vec4(r,g,b,a));
    }


    static Vertex Build(Math3D::Vec3 pos = Math3D::Vec3::zero(),Math3D::Vec4 color = Math3D::Vec4(1,1,1,1), Math3D::Vec3 normal = Math3D::Vec3::zero(), Math3D::Vec2 texcords = Math3D::Vec2(0,0)){
        Vertex vtx;
        vtx.Position = pos;
        vtx.Normal = normal;
        vtx.Color = color;
        vtx.TexCoords = texcords;
        //Math3D::Random rand;
        //vtx.id = (rand.next<uint16_t>(INT16_MAX));
        return vtx;
    }

    static Vertex Compose(std::vector<float>& floats, int dataSize = VERTEX_DATA_WIDTH /* XYZRGBANNUV */){
        std::vector<float> first12;
        for(int i = 0; i < floats.size(); i++){
            if(i < dataSize){
                first12.push_back(floats[i]);
            }else{
                break;
            }
        }


        Vertex vtx;
        if(first12.size() >= 3){
            vtx.Position.x = first12[0];
            vtx.Position.y = first12[1];
            vtx.Position.z = first12[2];
        }

        if(first12.size() >= 7){
            vtx.Color.x = first12[3];
            vtx.Color.y = first12[4];
            vtx.Color.z = first12[5];
            vtx.Color.w = first12[6];
        }

        if(first12.size() >= 10){
            vtx.Normal.x = first12[7];
            vtx.Normal.y = first12[8];
            vtx.Normal.z = first12[9];
        }

        if(first12.size() >= 12){
            vtx.TexCoords.x = first12[10];
            vtx.TexCoords.y = first12[11];
        }

        //Math3D::Random rand;
        //vtx.id = (rand.next<uint16_t>(INT16_MAX));

        return vtx;
    }

    static std::vector<float> Decompose(Vertex &v){
        std::vector<float> vertexVector;
        /* XYZRGBANNUV */
        vertexVector.push_back(v.Position.x);
        vertexVector.push_back(v.Position.y);
        vertexVector.push_back(v.Position.z);
        vertexVector.push_back(v.Color.x);
        vertexVector.push_back(v.Color.y);
        vertexVector.push_back(v.Color.z);
        vertexVector.push_back(v.Color.w);
        vertexVector.push_back(v.Normal.x);
        vertexVector.push_back(v.Normal.y);
        vertexVector.push_back(v.Normal.z);
        vertexVector.push_back(v.TexCoords.x);
        vertexVector.push_back(v.TexCoords.y);

        return vertexVector;
    }

    bool operator==(const Vertex& vtx) const {
        return
            Math3D::AreClose(Position.x, vtx.Position.x) &&
            Math3D::AreClose(Position.y, vtx.Position.y) &&
            Math3D::AreClose(Position.z, vtx.Position.z) &&

            Math3D::AreClose(Normal.x, vtx.Normal.x) &&
            Math3D::AreClose(Normal.y, vtx.Normal.y) &&
            Math3D::AreClose(Normal.z, vtx.Normal.z) &&

            Math3D::AreClose(TexCoords.x, vtx.TexCoords.x) &&
            Math3D::AreClose(TexCoords.y, vtx.TexCoords.y)  &&
            Math3D::AreClose(Color.x, vtx.Color.x) &&
            Math3D::AreClose(Color.y, vtx.Color.y)  &&
            Math3D::AreClose(Color.z, vtx.Color.z)  &&
            Math3D::AreClose(Color.w, vtx.Color.w);
    }

    bool operator!=(const Vertex& vtx) const {
        return !(*this == vtx);
    }
};

class Mesh : public IDrawable{
    private:
        std::vector<Vertex> verticies;
        std::vector<uint32_t> faces;
        VertexBufferObject VBO = 0;
        VertexArrayObject VAO = 0;
        ElementBufferObject EBO = 0;

        void _genBuffers();
        bool _areBuffersBound();

    public:
        Mesh(){};
        Mesh(const Mesh&) = delete;
        Mesh(Mesh&& other) noexcept;

        void upload(std::vector<Vertex> verts, std::vector<uint32_t> faces);
        void reload();

        void bind();
        void draw(
            const Math3D::Mat4& parent = Math3D::Mat4(),
            const Math3D::Mat4& view = Math3D::Mat4(),
            const Math3D::Mat4& projection = Math3D::Mat4()
        ) override;

        void dispose();
        
        std::vector<Math3D::Vec3> getVerticiesVectorArray();
        std::vector<float> getVerticiesAsRawFloatArray();
        std::vector<float> getRawVertexData();

        void setLocationAttributeOffset(int attribute, int dataSize, int dataOffset);

        std::vector<Vertex>& getVertecies() {return this->verticies;}

        static void Unbind();

        ~Mesh();
};

#endif // MESH_H
