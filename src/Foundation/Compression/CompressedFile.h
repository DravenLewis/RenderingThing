/**
 * @file src/Foundation/Compression/CompressedFile.h
 * @brief Declarations for CompressedFile.
 */

#ifndef COMPRESSED_FILE_H
#define COMPRESSED_FILE_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "Foundation/Util/Types.h"

/// @brief Represents the CompressedFile type.
class CompressedFile {
    public:
        /// @brief Enumerates values for Format.
        enum class Format : int {
            Unknown = 0,
            Zip
        };

        /// @brief Holds data for Entry.
        struct Entry {
            std::string path;
            std::uint16_t compressionMethod = 0;
            std::uint32_t crc32 = 0;
            std::uint32_t compressedSize = 0;
            std::uint32_t uncompressedSize = 0;
            std::uint32_t localHeaderOffset = 0;
            bool isDirectory = false;
        };

        /// @brief Holds data for WriteEntry.
        struct WriteEntry {
            std::string path;
            BinaryBuffer data;
            bool isDirectory = false;
            bool preferCompression = true;
        };

    private:
        Format currentFormat = Format::Unknown;
        std::filesystem::path archivePath;
        BinaryBuffer archiveBytes;
        std::vector<Entry> entryList;
        std::map<std::string, size_t> entryIndexByPath;

        /**
         * @brief Checks whether parse zip.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool parseZip(std::string* outError);
        /**
         * @brief Checks whether locate zip end of central directory.
         * @param outOffset Zero-based offset value.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool locateZipEndOfCentralDirectory(size_t& outOffset, std::string* outError) const;
        /**
         * @brief Checks whether parse zip entry.
         * @param centralDirectoryOffset Zero-based offset value.
         * @param outEntry Output value for entry.
         * @param outNextOffset Zero-based offset value.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool parseZipEntry(size_t centralDirectoryOffset, Entry& outEntry, size_t& outNextOffset, std::string* outError) const;
        /**
         * @brief Reads local file payload.
         * @param entry Value for entry.
         * @param outData Buffer that receives data data.
         * @param outSize Number of elements or bytes.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool readLocalFilePayload(const Entry& entry, const uint8_t*& outData, size_t& outSize, std::string* outError) const;

    public:
        /**
         * @brief Constructs a new CompressedFile instance.
         */
        CompressedFile() = default;

        /**
         * @brief Opens this object.
         * @param path Filesystem path for path.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool open(const std::filesystem::path& path, std::string* outError = nullptr);
        /**
         * @brief Writes data to the destination.
         * @param entries Value for entries.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool write(const std::vector<WriteEntry>& entries, std::string* outError = nullptr);
        /**
         * @brief Writes to path.
         * @param path Filesystem path for path.
         * @param entries Value for entries.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool writeToPath(const std::filesystem::path& path, const std::vector<WriteEntry>& entries, std::string* outError = nullptr);

        /**
         * @brief Checks whether open.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool isOpen() const { return !archivePath.empty(); }
        Format format() const { return currentFormat; }
        const std::filesystem::path& path() const { return archivePath; }
        const std::vector<Entry>& entries() const { return entryList; }
        bool hasEntry(const std::string& entryPath) const;
        const Entry* findEntry(const std::string& entryPath) const;
        bool readEntry(const std::string& entryPath, BinaryBuffer& outData, std::string* outError = nullptr) const;

        static std::string NormalizeEntryPath(const std::string& path, bool forceDirectory = false);
        static std::uint32_t ComputeCRC32(const uint8_t* data, size_t size);
        static std::uint32_t ComputeCRC32(const BinaryBuffer& data);
    };

#endif // COMPRESSED_FILE_H
