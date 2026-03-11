/**
 * @file src/Foundation/IO/File.h
 * @brief Declarations for File.
 */

#ifndef FILE_H
#define FILE_H

#include <string>
#include <fstream>
#include <cstdint>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>

#include "Foundation/Util/Types.h"

#define FILE_SEPARATOR "\\"

/// @brief Represents the File type.
class File{
    private:
        std::string filepath;
        bool isVirtual;
        bool hasVirtualData = false;
        BinaryBuffer virtualData;
        std::fstream fileStream;
        /**
         * @brief Constructs a new File instance.
         */
        File() = delete;

    public:
        /**
         * @brief Constructs a new File instance.
         * @param path Filesystem path for path.
         */
        File(std::string path);
        /**
         * @brief Constructs a new File instance.
         * @param path Filesystem path for path.
         * @param inMemoryData Input parameter.
         */
        File(std::string path, const BinaryBuffer& inMemoryData);
        /**
         * @brief Executes exists.
         * @return True when the operation succeeds; otherwise false.
         */
        bool exists();
        /**
         * @brief Checks whether directory.
         * @return True when the operation succeeds; otherwise false.
         */
        bool isDirectory();
        /**
         * @brief Gets the file name.
         * @return Resulting string value.
         */
        std::string getFileName();
        /**
         * @brief Gets the file type.
         * @return Resulting string value.
         */
        std::string getFileType();
        /**
         * @brief Creates file.
         * @return True when the operation succeeds; otherwise false.
         */
        bool createFile();
        /**
         * @brief Executes delete file.
         * @return True when the operation succeeds; otherwise false.
         */
        bool deleteFile();
        /**
         * @brief Executes open.
         * @param options Input parameter.
         * @return True when the operation succeeds; otherwise false.
         */
        bool open(std::ios::openmode options);
        /**
         * @brief Gets the stream.
         * @return Result of this operation.
         */
        std::fstream& getStream();
        /**
         * @brief Checks whether open.
         * @return True when the operation succeeds; otherwise false.
         */
        bool isOpen();
        /**
         * @brief Executes close.
         */
        void close();
        /**
         * @brief Gets the path.
         * @return Resulting string value.
         */
        std::string& getPath();
        /**
         * @brief Checks whether in memory file.
         * @return True when the operation succeeds; otherwise false.
         */
        bool isInMemoryFile() const;
        /**
         * @brief Gets the in memory data.
         * @return Result of this operation.
         */
        const BinaryBuffer& getInMemoryData() const;
        /**
         * @brief Gets the cwd.
         * @return Resulting string value.
         */
        static std::string GetCWD();
};

/// @brief Holds data for FileBlob.
struct FileBlob{
    BinaryBuffer data;

    // Helper to get string representation on the fly
    // This avoids storing the same data twice
    std::string asString() const {
        return std::string(data.begin(), data.end());
    }

    /**
     * @brief Returns the data size.
     * @return Computed numeric result.
     */
    size_t size() const {
        return data.size();
    }

    /**
     * @brief Creates a new object.
     * @param size Number of elements or bytes.
     * @return Result of this operation.
     */
    static FileBlob Create(int size){
        FileBlob fblob;
        fblob.data.resize(size);
        return fblob;
    }

    /**
     * @brief Creates a new object.
     * @param fromString Value for from string.
     * @return Result of this operation.
     */
    static FileBlob Create(const char* fromString) {
        if (!fromString) return {};
        
        FileBlob fblob;
        size_t length = std::strlen(fromString);
        fblob.data.assign(fromString, fromString + length);
        return fblob;
    }

    /**
     * @brief Creates a new object.
     * @param dataBuffer Value for data buffer.
     * @return Result of this operation.
     */
    static FileBlob Create(const BinaryBuffer& dataBuffer) {
        FileBlob fblob;
        fblob.data = dataBuffer; // Efficiently copies the data
        return fblob;
    }
};

/// @brief Holds data for LineEnding.
struct LineEnding{
    private:
        const char* ending;
    public:
        /**
         * @brief Constructs a new LineEnding instance.
         * @param ending Value for ending.
         */
        LineEnding(const char* ending){
            this->ending = ending;
        }

        /**
         * @brief Returns the current value.
         * @return Pointer to the resulting object.
         */
        const char* get(){
            return this->ending;
        }

        static const LineEnding UNIX;
        static const LineEnding WINDOWS;
};

/// @brief Represents the FileReader type.
class FileReader{
    public:
        /**
         * @brief Reads data from the source.
         * @param file Filesystem path for file.
         * @return Result of this operation.
         */
        static FileBlob Read(File *file);
};

/// @brief Represents the FileWriter type.
class FileWriter{
    private:
        std::shared_ptr<File> filePtr;
        std::vector<uint8_t> buffer;
    public:
        /**
         * @brief Constructs a new FileWriter instance.
         * @param filePtr Pointer to file.
         */
        FileWriter(File* filePtr);
        /**
         * @brief Writes byte.
         * @param byte Input parameter.
         * @param offset Spatial value used by this operation.
         */
        void writeByte(uint8_t byte, int offset);
        /**
         * @brief Executes append byte.
         * @param byte Input parameter.
         */
        void appendByte(uint8_t byte);
        /**
         * @brief Writes data.
         * @param data Input parameter.
         * @param offset Spatial value used by this operation.
         * @param dataLength Input parameter.
         */
        void writeData(uint8_t* data, int offset, int dataLength);
        /**
         * @brief Executes append data.
         * @param data Input parameter.
         * @param dataLength Input parameter.
         */
        void appendData(uint8_t* data, int dataLength);
        /**
         * @brief Writes blob.
         * @param blob Input parameter.
         * @param offset Spatial value used by this operation.
         */
        void writeBlob(FileBlob &blob, int offset);
        /**
         * @brief Executes append blob.
         * @param blob Input parameter.
         */
        void appendBlob(FileBlob &blob);
        /**
         * @brief Executes put.
         * @param string Input parameter.
         */
        void put(const char* string);
        /**
         * @brief Executes putln.
         * @param string Input parameter.
         * @param LineEnding Input parameter.
         */
        void putln(const char* string, LineEnding LineEnding = LineEnding::UNIX);
        /**
         * @brief Executes flush.
         * @return True when the operation succeeds; otherwise false.
         */
        bool flush();
        /**
         * @brief Executes close.
         * @return True when the operation succeeds; otherwise false.
         */
        bool close();

};

#endif // FILE_H
