#include "Utils.h"

std::string readFile(const std::string& filePath)
{
    std::ifstream inFile(filePath);

    if (!inFile.is_open())
    {
        throw std::runtime_error("Cannot open file: " + filePath);
    }

    try
    {
        // read the file
        inFile.seekg(0, std::ios::end);
        size_t fileSize = inFile.tellg();
        inFile.seekg(0, std::ios::beg);

        // preallocate memory
        std::string content;
        content.reserve(fileSize);

        // read the file
        content.assign((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());

        inFile.close();
        return content;
    }
    catch (const std::exception& e)
    {
        inFile.close();
        throw std::runtime_error("Error reading file: " + filePath + "\n" + e.what());
    }
}

void saveFile(const std::string& filePath, const std::string& content)
{
    std::ofstream outFile(filePath, std::ios::out | std::ios::trunc);

    if (!outFile.is_open())
    {
        throw std::runtime_error("Cannot open file for writing: " + filePath);
    }

    try
    {
        outFile << content;

        // check if the write operation was successful
        if (outFile.fail())
        {
            throw std::runtime_error("Failed to write content to file");
        }

        outFile.close();
    }
    catch (const std::exception& e)
    {
        outFile.close();
        throw std::runtime_error("Error writing to file: " + filePath + "\n" + e.what());
    }
}
