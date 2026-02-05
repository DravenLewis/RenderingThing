#ifndef TYPES_H
#define TYPES_H

#include <vector>
#include <cstdint>
#include <memory>

typedef std::vector<uint8_t> BinaryBuffer;


template<typename T>
class Nullable{
    private:
        std::shared_ptr<T> tPtr;

    public:
        
        Nullable() : tPtr(nullptr) {};
        Nullable(std::nullptr_t) : tPtr(nullptr) {};
        Nullable(std::shared_ptr<T> valuePtr) : tPtr(valuePtr) {};
        Nullable(const T& valueRef) : tPtr(std::make_shared<T>(valueRef)) {};

        T getValueOrDefault(const T& defaultValue) const {
            return (tPtr) ? *tPtr : defaultValue;
        }

        bool hasValue() const { return tPtr != nullptr;}

        explicit operator bool() const {return tPtr != nullptr;}

        T& operator*() { return *tPtr; }
        const T& operator*() const { return *tPtr; }

        T* operator->() { return tPtr.get(); }
        const T* operator->() const { return tPtr.get(); }

        bool operator==(const Nullable<T>& other) const {
            if(!tPtr && !other.tPtr) return true;
            if(!tPtr || !other.tPtr) return false;
            return *tPtr == *other.tPtr;
        }

        bool operator!=(const Nullable<T>& other) const {
            return !(*this == other);
        }

        bool operator==(std::nullptr_t) const { return !tPtr; };
        bool operator!=(std::nullptr_t) const { return tPtr != nullptr;}

};

#endif // TYPES_H