#include "File.h"
#include "Logbot.h"
#include "StringUtils.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <exception>
#include <windows.h>

const LineEnding LineEnding::UNIX("\n");
const LineEnding LineEnding::WINDOWS("\r\n");

std::string File::GetCWD(){
    try{
        #ifdef _WIN32
            // Windows: Get the path to the current executable
            char result[MAX_PATH];
            GetModuleFileNameA(NULL, result, MAX_PATH);
            std::filesystem::path exePath(result);
            std::filesystem::path exeDir = exePath.parent_path();
            std::string pathStr = std::string(exeDir.u8string());
            //LogBot.LogVerbose(LOG_INFO, "Executable directory: %s", pathStr.c_str());
            return pathStr;
        #else
            // Other platforms: Use current working directory
            std::filesystem::path currentPath = std::filesystem::current_path();
            std::string pathStr = std::string(currentPath.u8string());
            LogBot.LogVerbose(LOG_INFO, "Current working directory: %s", pathStr.c_str());
            return pathStr;
        #endif
    }catch(std::filesystem::filesystem_error const& ex){
        LogBot.LogVerbose(LOG_FATL,"Failed to Resolve Current Working Directory: %s", ex.what());
    }

    return "";
}

File::File(std::string pathName){
    this->isVirtual = false;  // Initialize first
    this->fileStream = std::fstream(pathName, std::ios::in);
    if(!this->fileStream) this->isVirtual = true;
    this->fileStream.close();
    this->filepath = pathName;
}

bool File::exists(){
    return !this->isVirtual;
}

bool File::isDirectory(){
    return std::filesystem::is_directory(this->filepath) || StringUtils::EndsWith(this->filepath,FILE_SEPARATOR);
}

std::string File::getFileName(){
    StringUtils::StringArray fileDirectories = StringUtils::Split(this->filepath,FILE_SEPARATOR);
    int length = fileDirectories.size();
    std::string lastInstance = fileDirectories[length - 1];

    return lastInstance;
}

std::string File::getFileType(){
     if(!isDirectory()){
        std::string name = getFileName();
        StringUtils::StringArray fileParts = StringUtils::Split(name, ".");
        if(fileParts.size() >= 1){
            return StringUtils::Format(".%s",fileParts[1].c_str());
        }
    }

    return "/";
}

bool File::createFile(){
    if(!exists()){
        if(isDirectory()){
            if(!std::filesystem::create_directories(this->filepath)){
                this->isVirtual = true;
                LogBot.Log(LOG_ERRO,"Failed to Create Directory: '%s'", this->filepath.c_str());
                return false;
            }else{
                LogBot.Log(LOG_INFO,"Directory '%s' Created.", this->filepath.c_str());
                return true;
            }
        }

        this->fileStream = std::fstream(this->filepath, std::ios::out);
        this->isVirtual = false;
        if(!this->fileStream){
            this->isVirtual = true;
            LogBot.Log(LOG_ERRO,"Failed to create file: %s, reverting to virtual.", this->filepath.c_str());
            return false;
        }else{
            LogBot.Log(LOG_INFO,"File '%s' created successfully.", this->filepath.c_str());
        }
        this->fileStream.close();
        return true;
    }
    LogBot.Log(LOG_WARN,"File already exists, ignoring.");
    return true;
}

bool File::deleteFile(){
    if(exists()){
        if(isDirectory()){
            try{
                unsigned long long count = std::filesystem::remove_all(this->filepath);
                LogBot.Log(LOG_INFO, "Sucessfully removed directory and %s items.", count);
                return true;
            }catch(const std::filesystem::filesystem_error& e){
                LogBot.Log(LOG_ERRO, "Failed to delete directory: %s", e.what());
                return false;
            }
        }

        try{
            bool success = std::filesystem::remove(this->filepath);
            if(success){
                LogBot.Log(LOG_INFO,"File '%s' created successfully.");
                return true;
            }else{
                LogBot.Log(LOG_WARN,"File '%s' could not be created for an unknown reason, file may be open or virtual.");
                return false;
            }
        }catch(std::filesystem::filesystem_error& e){
            LogBot.Log(LOG_ERRO, "Failed to delete file: %s", e.what());
            return false;
        }
    }
    LogBot.Log(LOG_WARN,"Cant delete virtual file, ignoring.");
    return false;
}

bool File::open(std::ios::openmode options){
    this->fileStream = std::fstream(this->filepath, options);
    if(!fileStream){
        return false;
    }

    return true;
}

std::fstream& File::getStream(){
    return this->fileStream;
}

bool File::isOpen(){
    return this->fileStream.is_open();
}

void File::close(){
    if(isOpen()){
        this->fileStream.close();
    }
}

std::string& File::getPath(){
    return this->filepath;
}

FileBlob FileReader::Read(File* filePtr){
    if(filePtr->exists() && !filePtr->isDirectory()){
        auto fileInputStream = std::fstream(filePtr->getPath(), std::ios::in | std::ios::binary);

        if(fileInputStream.is_open()){
            std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(fileInputStream), {});
            FileBlob fblob = FileBlob::Create(buffer.size());

            for(int i = 0; i < buffer.size(); i++){
                uint8_t value = buffer[i];
                fblob.data[i] = value;
            }

            fileInputStream.close();

            return fblob;
        }else{
            LogBot.LogVerbose(LOG_ERRO, "File stream failed to open: %s", filePtr->getPath().c_str());
        }
    }else{
        LogBot.LogVerbose(LOG_ERRO, "File does not exist or is directory: %s (exists: %d, isDir: %d)", filePtr->getPath().c_str(), filePtr->exists(), filePtr->isDirectory());
    }

    throw std::runtime_error("Unable to Read File.");
}

FileWriter::FileWriter(File* filePtr){
    this->filePtr = std::shared_ptr<File>(filePtr);
    if(!this->filePtr->isOpen()){
        if(!this->filePtr->exists()){
            // File Doesnt Exist. Try and make it.
            if(!this->filePtr->createFile()){
                LogBot.Log(LOG_INFO, "Failed to create file, may already exist.");
            }
        }

        if(!this->filePtr->open(std::ios::out | std::ios::binary)) throw std::runtime_error("File could not be opened.");
    }
    // File Exists and is open.
}
void FileWriter::writeByte(uint8_t byte, int offset){
    if(offset > this->buffer.size() - 1){
        this->buffer.resize(offset);
    }
    this->buffer[offset] = byte;
}

void FileWriter::appendByte(uint8_t byte){
    this->buffer.push_back(byte);
}

void FileWriter::writeData(uint8_t* data, int offset, int dataLength){
    if(offset + dataLength > this->buffer.size()){
        this->buffer.resize(offset + dataLength);
    }

    int index = 0;
    for(auto i = offset; i < offset + dataLength; i++){
        this->buffer[i] = data[index];
        index++;
    }
}

void FileWriter::appendData(uint8_t* data, int dataLength){
    for(auto i = 0; i < dataLength; i++){
        this->buffer.push_back(data[i]);
    }
}

void FileWriter::writeBlob(FileBlob &blob, int offset){
    writeData(blob.data.data(),offset,blob.size());
}

void FileWriter::appendBlob(FileBlob &blob){
    appendData(blob.data.data(), blob.size());
}

void FileWriter::put(const char* string){
    appendData(reinterpret_cast<uint8_t*>(const_cast<char*>(string)), std::strlen(string));
}

void FileWriter::putln(const char* string, LineEnding lineEnding){
    std::string finalString = std::string(string) + lineEnding.get();
    put(finalString.c_str());
}

bool FileWriter::flush(){

    if(this->filePtr->isOpen()){
        std::fstream &fileStream = this->filePtr->getStream();
        try{
            fileStream.write(reinterpret_cast<const char*>(this->buffer.data()), this->buffer.size());
            fileStream.flush();
            return true;
        }catch(std::exception &e){
            LogBot.Log(LOG_ERRO, "Failed to write file: %s", e.what());
            return false;
        }
    }
    return false;
}

bool FileWriter::close(){
    if(!this->filePtr->isOpen()) return true;
    this->filePtr->close();
    return true;
}