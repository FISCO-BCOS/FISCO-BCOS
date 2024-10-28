#pragma once
#include <fstream>
#include <string>

std::string readFile(const std::string& filePath);

void saveFile(const std::string& filePath, const std::string& content);
