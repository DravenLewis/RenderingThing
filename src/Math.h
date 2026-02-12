#ifndef $MATH_H_
#define $MATH_H_

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp> // Required for decomposition

#include <random>
#include <chrono>
#include <type_traits>

/**
 * Wrapper class for GLM Math.
 */

namespace Math3D{

    inline static const float PI = 3.14159265359f; // Close enough.
    inline static const float EPSILON = 1e-5f;

    inline float Sin(float i){ return glm::sin(i); }
    inline float Cos(float i){ return glm::cos(i); }
    inline float Tan(float i){ return glm::tan(i); }

    inline float HSin(float i){ return glm::sinh(i); }
    inline float HCos(float i){ return glm::cosh(i); }
    inline float HTan(float i){ return glm::tanh(i); }

    inline float ASin(float i){ return glm::asin(i); }
    inline float ACos(float i){ return glm::acos(i); }
    inline float ATan(float i){ return glm::atan(i); }

    inline float AHSin(float i){ return glm::asinh(i); }
    inline float AHCos(float i){ return glm::acosh(i); }
    inline float AHTan(float i){ return glm::atanh(i); }

    static bool AreClose(float a, float b, float eps = EPSILON){
        return glm::abs(a - b) <= eps;
    }

    

    template<typename T>
    inline T Min(const T& lvalue, const T& rvalue){
        if(lvalue < rvalue) return lvalue;
        return rvalue;
    }

    template<typename T>
    inline T Max(const T& lvalue, const T& rvalue){
        if(lvalue > rvalue) return lvalue;
        return rvalue;
    }

    template<typename T>
    inline T Clamp(const T& value, const T& minValue, const T& maxValue){
        return Min(maxValue, Max(minValue, value));
    }

    template<typename T, typename U>
    inline T Lerp(const T& start, const T& end, const U& time){
        return start + (end - start) * time;
    }

    template<typename T>
    inline T Slerp(const T& value, const T& minValue, const T& maxValue){
        return Min(maxValue, Max(minValue, value));
    }

    // Forward declarations
    struct Vec3;
    struct Vec4;
    struct Mat4;
    struct Quat;

    // --- Vector 2 ---
    struct Vec2 {
        float x, y;
        Vec2(float x = 0.f, float y = 0.f) : x(x), y(y) {}
        Vec2(const glm::vec2& v) : x(v.x), y(v.y) {}

        operator glm::vec2() const { return glm::vec2(x, y); }

        Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
        Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
        Vec2 operator-() const { return Vec2(-x, -y); }

        Vec2 operator*(float s)       const { return Vec2(x * s, y * s); }
        bool operator==(const Vec2&t) const {return (x == t.x && y == t.y);}
        bool operator!=(const Vec2&t) const {return (x != t.x || y != t.y);}
        float length() const { return glm::length((glm::vec2)*this); }
        Vec2 normalize() const { return Vec2(glm::normalize((glm::vec2)*this)); }
    };

    // --- Vector 3 ---
    struct Vec3 {
        float x, y, z;
        Vec3(float x = 0.f, float y = 0.f, float z = 0.f) : x(x), y(y), z(z) {}
        Vec3(float v) : x(v), y(v), z(v) {}
        Vec3(const glm::vec3& v) : x(v.x), y(v.y), z(v.z) {}

        operator glm::vec3() const { return glm::vec3(x, y, z); }

        // Operators
        Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
        Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
        Vec3 operator-() const { return Vec3(-x, -y, -z); }
        Vec3 operator*(const Vec3& o) const { return Vec3(x * o.x, y * o.y, z * o.z); } // Component-wise
        Vec3 operator*(float s)       const { return Vec3(x * s, y * s, z * s); }
        Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
        Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
        bool operator==(const Vec3&t) const {return (x == t.x && y == t.y && z == t.z);}
        bool operator!=(const Vec3&t) const {return (x != t.x || y != t.y || z != t.z);}

        // Constants
        static Vec3 zero()  { return Vec3(0,0,0); }
        static Vec3 one()   { return Vec3(1,1,1); }
        static Vec3 up()    { return Vec3(0,1,0); }
        static Vec3 right() { return Vec3(1,0,0); }
        static Vec3 forward(){ return Vec3(0,0,1); } // OpenGL default forward is +Z or -Z depending on handedness

        // Helpers
        float length() const { return glm::length((glm::vec3)*this); }
        Vec3 normalize() const { return Vec3(glm::normalize((glm::vec3)*this)); }
        
        static float dot(const Vec3& a, const Vec3& b) { return glm::dot((glm::vec3)a, (glm::vec3)b); }
        static Vec3 cross(const Vec3& a, const Vec3& b) { return Vec3(glm::cross((glm::vec3)a, (glm::vec3)b)); }
        static float distance(const Vec3& a, const Vec3& b) { return glm::distance((glm::vec3)a, (glm::vec3)b); }

        static Vec3 Slerp(const Vec3& start, const Vec3& end, float time){
            float dot = Clamp(Vec3::dot(start,end),-1.0f,1.0f);

            float theta =  ACos(dot) * time;
            Vec3 rel = (end - start * dot).normalize();

            return ((start * Cos(theta)) + (rel * Sin(theta)));
        }
    };

    // --- Vector 4 ---
    struct Vec4 {
        float x, y, z, w;
        Vec4(float x = 0.f, float y = 0.f, float z = 0.f, float w = 1.f) : x(x), y(y), z(z), w(w) {}
        Vec4(const Vec3& v, float w = 1.f) : x(v.x), y(v.y), z(v.z), w(w) {}
        Vec4(const glm::vec4& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}

        operator glm::vec4() const { return glm::vec4(x, y, z, w); }

        Vec4 operator-() const { return Vec4(-x, -y, -z, -w); }
        bool operator==(const Vec4&t) const {return (x == t.x && y == t.y && z == t.z && w == t.w);}
        bool operator!=(const Vec4&t) const {return (x != t.x || y != t.y || z != t.z || w != t.w);}
    };

    // --- Quaternion ---
    struct Quat {
        float x, y, z, w; // GLM Quat is w, x, y, z in constructor usually, but stored as x,y,z,w
        
        // Identity quaternion
        Quat() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
        Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
        Quat(const glm::quat& q) : x(q.x), y(q.y), z(q.z), w(q.w) {}

        // Create from Euler Angles (Degrees)
        static Quat FromEuler(const Vec3& eulerAngles) {
            // GLM expects radians
            glm::quat q = glm::quat(glm::radians((glm::vec3)eulerAngles)); 
            return Quat(q);
        }

        // Create from Angle-Axis
        static Quat AngleAxis(float angleDeg, const Vec3& axis) {
            return Quat(glm::angleAxis(glm::radians(angleDeg), (glm::vec3)axis));
        }

        // Create from 3x3 rotation matrix
        static Quat FromMat3(const glm::mat3& m) {
            return Quat(glm::quat_cast(m));
        }

        // Inside struct Quat
        Quat normalize() const {
            glm::quat q = (glm::quat)*this;
            q = glm::normalize(q);
            return Quat(q);
        }

        static Quat Lerp(const Quat& start, const Quat& end, float time){
            return Quat(glm::mix((glm::quat)start, (glm::quat)end, time));
        }

        static Quat Slerp(const Quat& start, const Quat& end, float time){
            return Quat(glm::slerp((glm::quat)start, (glm::quat)end, time));
        }

        // Convert to Euler angles (Degrees)
        Vec3 ToEuler() const {
            glm::vec3 eulerRad = glm::eulerAngles((glm::quat)*this);
            glm::vec3 eulerDeg = glm::degrees(eulerRad);
            return Vec3(eulerDeg.x, eulerDeg.y, eulerDeg.z);
        }

        operator glm::quat() const { return glm::quat(w, x, y, z); } // Note GLM constructor order w,x,y,z

        // Rotate a vector by this quaternion
        Vec3 operator*(const Vec3& v) const {
            return Vec3(glm::rotate((glm::quat)*this, (glm::vec3)v));
        }

        // Combine rotations (q2 * q1 means rotate by q1 then q2)
        Quat operator*(const Quat& o) const {
            return Quat((glm::quat)*this * (glm::quat)o);
        }
        
        // Helper
        glm::mat4 toMat4() const { return glm::toMat4((glm::quat)*this); }
    };

    struct Mat4 {
        glm::mat4 data;

        Mat4(float diagonal = 1.0f) : data(glm::mat4(diagonal)) {}
        Mat4(const glm::mat4& m) : data(m) {}
        operator glm::mat4() const { return data; }

        // --- NEW: Robust Decomposition ---
        // Extracts P/R/S from a Matrix, even if it was skewed.
        // Note: Skew information is lost in this conversion (approximated into Scale/Rot).
        void decompose(Vec3& outPos, Quat& outRot, Vec3& outScale) const {
            glm::vec3 scale, translation, skew;
            glm::vec4 perspective;
            glm::quat orientation;
            
            glm::decompose(data, scale, orientation, translation, skew, perspective);
            
            outPos = Vec3(translation);
            outRot = Quat(orientation);
            outScale = Vec3(scale);
        }

        // Quick helpers if you only need one component from a complex matrix
        Vec3 getPosition() const { return Vec3(data[3]); } // Last column is position
        
        // Operators
        Mat4 operator*(const Mat4& o) const { return Mat4(data * o.data); }
    };

    // --- Robust Transform ---
    struct Transform {
        Vec3 position;
        Quat rotation;
        Vec3 scale;

        Transform(Vec3 pos = Vec3::zero(), Quat rot = Quat(), Vec3 scl = Vec3::one())
            : position(pos), rotation(rot), scale(scl) {}

        // 1. Convert to Matrix (The Source of Truth)
        Mat4 toMat4() const {
            // Translate * Rotate * Scale
            glm::mat4 m = glm::mat4(1.0f);
            m = glm::translate(m, (glm::vec3)position);
            m *= glm::toMat4((glm::quat)rotation);
            m = glm::scale(m, (glm::vec3)scale);
            return Mat4(m);
        }

        // 2. The Robust Combination
        // Instead of returning a 'Transform', we return a 'Mat4'.
        // This preserves any skewing caused by non-uniform parent scaling.
        Mat4 operator*(const Transform& child) const {
            return this->toMat4() * child.toMat4();
        }

        // 3. Directions (Local Space)
        Vec3 forward() const { return rotation * Vec3(0, 0, 1); }
        Vec3 right()   const { return rotation * Vec3(1, 0, 0); }
        Vec3 up()      const { return rotation * Vec3(0, 1, 0); }

        // 4. Helper to set rotation from Euler easily
        void setRotation(float x, float y, float z) {
            rotation = Quat::FromEuler(Vec3(x, y, z));
        }

        void setRotation(const Vec3& eulerAngles) {
            rotation = Quat::FromEuler(eulerAngles);
        }

        // ===== ADDED HELPERS (minimal, non-invasive) =====

        // Position setters / movers
        void setPosition(const Vec3& p) { position = p; }
        void setPosition(float x, float y, float z) { position = Vec3(x,y,z); }
        void translate(const Vec3& delta) { position += delta; }
        void translate(float x = 0, float y = 0, float z = 0) { position += Vec3(x,y,z); }
        void setX(float x) { position.x = x; }
        void setY(float y) { position.y = y; }
        void setZ(float z) { position.z = z; }

        // Scale setters
        void setScale(const Vec3& s) { scale = s; }
        void setUniformScale(float s) { scale = Vec3(s, s, s); }
        void setScale(float x = 1.0f, float y = 1.0f, float z = 1.0f) {scale = Vec3(x, y, z); }

        // Rotate around an arbitrary axis (angle in radians)
        void rotateAxisAngle(const Vec3& axis, float angleDeg, bool localSpace = true) {
            Quat q = Quat::AngleAxis(angleDeg, axis);

            if(localSpace){
                rotation = rotation * q;
            }else{
                rotation = q * rotation;
            }

            rotation = rotation.normalize();
        }

        // Set rotation from forward and up (construct a rotation where local +Z points to forward)
        void lookAt(const Vec3& target, const Vec3& worldUp = Vec3(0,1,0)) {
            Vec3 dir = target - position;
            // avoid degenerate case
            float lengthSquared = dir.length() * dir.length();
            if (lengthSquared < 1e-8f) return;

            Vec3 f = dir.normalize();
            Vec3 up = worldUp.normalize();

            // Handle near-parallel forward/up
            if (fabs(Vec3::dot(f, up)) > 0.999f) {
                up = Vec3::cross(f, Vec3::right()).normalize();
                float upLengthSquared = up.length() * up.length();
                if (upLengthSquared < 1e-6f)
                    up = Vec3::cross(f, Vec3::forward()).normalize();
            }

            Vec3 r = Vec3::cross(up,f).normalize(); // right
            Vec3 u = Vec3::cross(f, r);                   // recomputed up (orthonormal)

            // Build rotation matrix where columns are (right, up, forward)
            glm::mat3 rotMat;
            rotMat[0] = (glm::vec3) r; // column 0
            rotMat[1] = (glm::vec3) u; // column 1
            rotMat[2] = (glm::vec3) f; // column 2

            rotation = Quat::FromMat3(rotMat);
        }

        // Build a transform from a Mat4 (decomposes translation / rotation / scale)
        static Transform fromMat4(const Mat4& m_in) {
            // assumes Mat4 can be casted to glm::mat4 via constructor like Mat4(glm::mat4)
            glm::mat4 m = (glm::mat4)m_in; // adjust cast if your Mat4 type differs

            // Extract translation
            glm::vec3 trans = glm::vec3(m[3]); // column-major: 4th column is translation

            // Extract scale from column lengths (note: this loses shear)
            glm::vec3 col0 = glm::vec3(m[0]);
            glm::vec3 col1 = glm::vec3(m[1]);
            glm::vec3 col2 = glm::vec3(m[2]);
            float sx = glm::length(col0);
            float sy = glm::length(col1);
            float sz = glm::length(col2);

            // Avoid division by zero
            glm::vec3 scaleVec(sx > 1e-8f ? sx : 0.0f,
                               sy > 1e-8f ? sy : 0.0f,
                               sz > 1e-8f ? sz : 0.0f);

            // Normalize rotation matrix columns to remove scale
            glm::mat3 rotMat;
            if (sx > 1e-8f) rotMat[0] = col0 / sx; else rotMat[0] = glm::vec3(1,0,0);
            if (sy > 1e-8f) rotMat[1] = col1 / sy; else rotMat[1] = glm::vec3(0,1,0);
            if (sz > 1e-8f) rotMat[2] = col2 / sz; else rotMat[2] = glm::vec3(0,0,1);

            Quat rot = (Quat)glm::quat_cast(rotMat);

            return Transform((Vec3)trans, rot, (Vec3)scaleVec);
        }

        // Convenience: set transform from mat directly
        void setFromMat4(const Mat4& m) {
            *this = Transform::fromMat4(m);
        }

        void reset(){
            this->setPosition(Math3D::Vec3(0,0));
            this->setScale(Math3D::Vec3(1,1,1));
            this->setRotation(0.0f,0.0f,0.0f);
        }

        // Quick utility: returns world-space position after applying a parent transform matrix
        static Vec3 transformPoint(const Mat4& parentMat, const Vec3& localPoint) {
            glm::vec4 p = (glm::mat4)parentMat * glm::vec4((glm::vec3)localPoint, 1.0f);
            return Vec3(p.x, p.y, p.z);
        }
    };

    struct Random{
        private:
            uint64_t seed;
            std::mt19937 rng;
        public:

            void setSeed(uint64_t seed){
                this->seed = seed;
                this->rng = std::mt19937(this->seed);
            }

            Random(uint64_t seed) {
                setSeed(seed);
            };
            Random() : Random(std::chrono::system_clock::now().time_since_epoch().count()) {};

            template<typename T>
            T range(T lowerBound, T upperBound){
                static_assert(std::is_arithmetic_v<T>, "Argument MUST be a Numeric Type");

                if constexpr (std::is_floating_point_v<T>){
                    std::uniform_real_distribution<T> dist(lowerBound, upperBound);
                    return dist(this->rng);
                }else{
                    using CommonInt = typename std::common_type<T, int>::type;
                    std::uniform_int_distribution<CommonInt> dist(lowerBound, upperBound);
                    return dist(this->rng);
                }

                std::uniform_int_distribution<> dist(lowerBound, upperBound);

                return dist(this->rng);
            }

            template<typename T>
            T next(T upperBound){
                return this->range<T>(0, upperBound);
            }
    };

    struct Rect{
        private:
            const int RECTANGLE_SIZE = 10;

        public:
            Vec2 position;
            Vec2 size;

            Rect(Vec2 pos, Vec2 size):
                position(pos.x,pos.y),
                size(size.x,size.y)
            {}

            Rect(float x, float y, float w, float h):
                position(x,y),
                size(w,h)
            {}

            Rect():
                position(0,0),
                size(RECTANGLE_SIZE,RECTANGLE_SIZE)
            {}

            bool intercects(const Rect& r){
                return !( 
                    position.x + size.x < r.position.x ||
                    position.x > r.position.x + r.size.x ||
                    position.y + size.y < r.position.y ||
                    position.y > r.position.y + r.size.y
                );
            }

            bool contains(const Rect& r){
                return (
                    r.position.x >= position.x &&
                    r.position.y >= position.y &&
                    r.position.x + r.size.x <= position.x + size.x &&
                    r.position.y + r.size.y <= position.y + size.y
                );
            }

            void operator= (const Rect& rect) {
                position.x = rect.position.x, 
                position.y = rect.position.y,
                size.x = rect.size.x;
                size.y = rect.size.y;
            }

            bool operator== (const Rect& rect) {
                return (
                    position.x == rect.position.x &&
                    position.y == rect.position.y &&
                    size.x == rect.size.x &&
                    size.y == rect.size.y
                );
            }

            bool operator!= (const Rect& rect) {
                return (
                    position.x != rect.position.x ||
                    position.y != rect.position.y ||
                    size.x != rect.size.x ||
                    size.y != rect.size.y
                );
            }
    };
};  

#endif // $MATH_H_
