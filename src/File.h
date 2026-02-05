#ifndef FILE_H
#define FILE_H

#include <string>
#include <fstream>
#include <cstdint>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>

#include "Types.h"

#define FILE_SEPARATOR "\\"

class File{
    private:
        std::string filepath;
        bool isVirtual;
        std::fstream fileStream;
        File() = delete;

    public:
        File(std::string path);
        bool exists();
        bool isDirectory();
        std::string getFileName();
        std::string getFileType();
        bool createFile();
        bool deleteFile();
        bool open(std::ios::openmode options);
        std::fstream& getStream();
        bool isOpen();
        void close();
        std::string& getPath();
        static std::string GetCWD();
};

struct FileBlob{
    BinaryBuffer data;

    // Helper to get string representation on the fly
    // This avoids storing the same data twice
    std::string asString() const {
        return std::string(data.begin(), data.end());
    }

    size_t size() const {
        return data.size();
    }

    static FileBlob Create(int size){
        FileBlob fblob;
        fblob.data.resize(size);
        return fblob;
    }

    static FileBlob Create(const char* fromString) {
        if (!fromString) return {};
        
        FileBlob fblob;
        size_t length = std::strlen(fromString);
        fblob.data.assign(fromString, fromString + length);
        return fblob;
    }

    static FileBlob Create(const BinaryBuffer& dataBuffer) {
        FileBlob fblob;
        fblob.data = dataBuffer; // Efficiently copies the data
        return fblob;
    }
};

struct LineEnding{
    private:
        const char* ending;
    public:
        LineEnding(const char* ending){
            this->ending = ending;
        }

        const char* get(){
            return this->ending;
        }

        static const LineEnding UNIX;
        static const LineEnding WINDOWS;
};

class FileReader{
    public:
        static FileBlob Read(File *file);
};

class FileWriter{
    private:
        std::shared_ptr<File> filePtr;
        std::vector<uint8_t> buffer;
    public:
        FileWriter(File* filePtr);
        void writeByte(uint8_t byte, int offset);
        void appendByte(uint8_t byte);
        void writeData(uint8_t* data, int offset, int dataLength);
        void appendData(uint8_t* data, int dataLength);
        void writeBlob(FileBlob &blob, int offset);
        void appendBlob(FileBlob &blob);
        void put(const char* string);
        void putln(const char* string, LineEnding LineEnding = LineEnding::UNIX);
        bool flush();
        bool close();

};

#endif // FILE_H