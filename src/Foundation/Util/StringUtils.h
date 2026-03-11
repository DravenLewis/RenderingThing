/**
 * @file src/Foundation/Util/StringUtils.h
 * @brief Declarations for StringUtils.
 */


#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <string>
#include <vector>
#include <algorithm>
#include <cstdarg> // for varargs support

/// @brief Represents the StringUtils type.
class StringUtils{    
    public:

        typedef std::vector<std::string> StringArray;

        /**
         * @brief Converts to lower case.
         * @param source Value for source.
         * @return Resulting string value.
         */
        static std::string ToLowerCase(const std::string& source){
            std::string newString = source;
            std::transform(newString.begin(), newString.end(), newString.begin(),[](unsigned char c){ // better than for looping.
                return std::tolower(c);
            });
            return newString;
        }

        /**
         * @brief Converts to upper case.
         * @param source Value for source.
         * @return Resulting string value.
         */
        static std::string ToUpperCase(const std::string& source){
            std::string newString = source;
            std::transform(newString.begin(), newString.end(), newString.begin(),[](unsigned char c){ // better than for looping.
                return std::toupper(c);
            });
            return newString;
        }

        /**
         * @brief Checks whether contains.
         * @param source Value for source.
         * @param value Value for value.
         * @return True when the operation succeeds; otherwise false.
         */
        static bool Contains(const std::string& source, std::string value){
            return source.find(value) != std::string::npos;
        }

        /**
         * @brief Checks whether contains ignore case.
         * @param source Value for source.
         * @param value Value for value.
         * @return True when the operation succeeds; otherwise false.
         */
        static bool ContainsIgnoreCase(const std::string& source, std::string value){
            std::string tempString = ToLowerCase(source);
            return tempString.find(ToLowerCase(value)) != std::string::npos;
        }

        /**
         * @brief Replaces the first matching substring.
         * @param source Value for source.
         * @param search Value for search.
         * @param value Value for value.
         * @return Resulting string value.
         */
        static std::string Replace(const std::string& source, std::string search, std::string value){
            if(Contains(source, search)){
                std::string newString = source;
                size_t foundLocation = newString.find(search);
                newString = newString.replace(foundLocation, search.length(), value);
                return newString;
            }
            return source;
        }

        /**
         * @brief Replaces all matching substrings.
         * @param source Value for source.
         * @param search Value for search.
         * @param value Value for value.
         * @return Resulting string value.
         */
        static std::string ReplaceAll(const std::string& source, std::string search, std::string value){
            if(Contains(source, search)){
                std::string newString = source;
                size_t index = 0;
                while((index = newString.find(search, index)) != std::string::npos){
                    newString = newString.replace(index, search.length(), value);
                    index += value.length();
                }

                return newString;
            }

            return source;
        }

        /**
         * @brief Checks whether ends with.
         * @param source Value for source.
         * @param value Value for value.
         * @return True when the operation succeeds; otherwise false.
         */
        static bool EndsWith(const std::string& source, std::string value){
            if(Contains(source, value) && source.length() >= value.length()){
                return (source.compare(source.length() - value.length(), value.length(), value)) == 0;
            }   
            
            return false;
        }

        /**
         * @brief Checks whether begins with.
         * @param source Value for source.
         * @param value Value for value.
         * @return True when the operation succeeds; otherwise false.
         */
        static bool BeginsWith(const std::string& source, std::string value){
            if(Contains(source, value) && source.length() >= value.length()){
                return (source.compare(0,value.length(), value) == 0);
            }   
            
            return false;
        }

        /**
         * @brief Trims leading and trailing whitespace.
         * @param source Value for source.
         * @return Resulting string value.
         */
        static std::string Trim(const std::string& source){
            const std::string whiteSpaceChars = " \n\r\t\f\v";
            std::string newString = source;

            // Trim the front of the String
            size_t firstNonWhiteSpaceChar = newString.find_first_not_of(whiteSpaceChars);
            if(firstNonWhiteSpaceChar == std::string::npos){ // Handle Empty / All Whitespace Strings.
                newString.clear();
                return newString;
            }

            newString.erase(0, firstNonWhiteSpaceChar); // Trim the actual first half.

            size_t lastNonWhiteSpaceChar = newString.find_last_not_of(whiteSpaceChars);
            if(lastNonWhiteSpaceChar != std::string::npos){
                newString.erase(lastNonWhiteSpaceChar + 1);
            }

            return newString;
        }

        /**
         * @brief Splits text into tokens.
         * @param source Value for source.
         * @param delimiter Value for delimiter.
         * @return Result of this operation.
         */
        static StringArray Split(const std::string& source, std::string delimiter){
            std::string stringCopy = source;
            StringArray splitParts;
            size_t index = 0;

            std::string currentSplit;
            while((index = stringCopy.find(delimiter)) != std::string::npos){ // find the delimiter
                currentSplit = stringCopy.substr(0,index); // split string from beginning to delimiter
                splitParts.push_back(currentSplit); // add the current section to the list
                stringCopy.erase(0, index + delimiter.length()); // remove the current split make whats after the new beginning
            }

            splitParts.push_back(stringCopy); // push the final part, the above will split away everything leaving us with the last part.

            return splitParts;
        }


        /**
         * @brief Formats a string using printf-style arguments.
         * @param source Value for source.
         * @param args Value for args.
         * @return Resulting string value.
         */
        template<typename... Args>
        static std::string Format(const std::string& source,Args... args){
            #pragma region Old Code
            /*va_list arguments; // OLD 
            va_start(arguments, source);

            va_list argumentsCopy;
            va_copy(argumentsCopy, arguments);

            int length = std::vsnprintf(0,0,source.c_str(), argumentsCopy);
            va_end(argumentsCopy); // done with the copy.

            if(length < 0){
                va_end(arguments); // we didnt have anything, so just ignore.
                return source;
            }

            std::vector<char> buffer(length + 1);

            std::vsnprintf(buffer.data(), buffer.size(), source.c_str(), arguments);
            va_end(arguments);

            return std::string(buffer.data());*/
            #pragma endregion

            int length = std::snprintf(nullptr,0,source.c_str(), std::forward<Args>(args)...);

            if(length <= 0){
                return source;
            }

            std::string result(length,'\0');
            std::snprintf(result.data(), length + 1, source.c_str(), std::forward<Args>(args)...);

            return result;
        }
};

#endif //STRING_UTILS_H
