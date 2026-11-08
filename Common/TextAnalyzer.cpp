#include "pch.h"
#include "TextAnalyzer.h"

namespace
{
	bool IsLatinLetter(const unsigned char value)
	{
		return (value >= 'A' && value <= 'Z') ||
			(value >= 'a' && value <= 'z');
	}

	bool IsAsciiDigit(const unsigned char value)
	{
		return value >= '0' && value <= '9';
	}

	bool IsWordCharacter(const unsigned char value)
	{
		return IsLatinLetter(value) || IsAsciiDigit(value);
	}
}

TextStatistics AnalyzeText(const std::string& text)
{
	TextStatistics statistics{};
	statistics.bytes = text.size();

	bool insideWord = false;

	for (const unsigned char value : text)
	{
		if (value == '\n')
		{
			++statistics.lines;
		}

		if (IsAsciiDigit(value))
		{
			++statistics.digits;
		}

		if (IsLatinLetter(value))
		{
			++statistics.latinLetters;
		}

		const bool isWordCharacter = IsWordCharacter(value);
		if (isWordCharacter && !insideWord)
		{
			++statistics.words;
		}

		insideWord = isWordCharacter;
	}

	if (!text.empty() && text.back() != '\n')
	{
		++statistics.lines;
	}

	return statistics;
}
