#pragma once

#include <cstddef>
#include <string>

struct TextStatistics
{
	std::size_t bytes;
	std::size_t lines;
	std::size_t words;
	std::size_t digits;
	std::size_t latinLetters;
};

TextStatistics AnalyzeText(const std::string& text);
