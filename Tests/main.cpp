#include <winsock2.h>

#include "Protocol.h"
#include "TextAnalyzer.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
	struct Expected
	{
		std::size_t bytes;
		std::size_t lines;
		std::size_t words;
		std::size_t digits;
		std::size_t latinLetters;
	};

	struct TestCase
	{
		std::string name;
		std::string text;
		Expected expected;
	};

	bool Matches(const TextStatistics& actual, const Expected& expected)
	{
		return actual.bytes == expected.bytes &&
			actual.lines == expected.lines &&
			actual.words == expected.words &&
			actual.digits == expected.digits &&
			actual.latinLetters == expected.latinLetters;
	}

	void PrintStatistics(const TextStatistics& statistics)
	{
		std::cout << "bytes=" << statistics.bytes
			<< ", lines=" << statistics.lines
			<< ", words=" << statistics.words
			<< ", digits=" << statistics.digits
			<< ", latinLetters=" << statistics.latinLetters;
	}
}

int main()
{
	const std::vector<TestCase> tests{
		{ "empty text", "", { 0, 0, 0, 0, 0 } },
		{ "one space", " ", { 1, 1, 0, 0, 0 } },
		{ "multiple spaces", "   ", { 3, 1, 0, 0, 0 } },
		{ "latin text", "Hello", { 5, 1, 1, 0, 5 } },
		{ "digits", "12345", { 5, 1, 1, 5, 0 } },
		{ "hyphens and punctuation", "x-y, z!", { 7, 1, 3, 0, 3 } },
		{ "multiple lines", "a\nb\nc", { 5, 3, 3, 0, 3 } },
		{ "trailing newline", "a\n", { 2, 1, 1, 0, 1 } },
		{ "Cyrillic text", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", { 12, 1, 0, 0, 0 } },
		{ "mixed text", "Ab\xD0\xB2 12", { 7, 1, 2, 2, 2 } },
		{ "maximum payload", std::string(kMaxPayloadSize, 'a'), { kMaxPayloadSize, 1, 1, 0, kMaxPayloadSize } }
	};

	int failed = 0;
	for (const TestCase& test : tests)
	{
		const TextStatistics actual = AnalyzeText(test.text);
		const bool passed = Matches(actual, test.expected);
		std::cout << (passed ? "PASS: " : "FAIL: ") << test.name << '\n';
		std::cout << "  expected: bytes=" << test.expected.bytes
			<< ", lines=" << test.expected.lines
			<< ", words=" << test.expected.words
			<< ", digits=" << test.expected.digits
			<< ", latinLetters=" << test.expected.latinLetters << '\n';
		std::cout << "  actual:   ";
		PrintStatistics(actual);
		std::cout << "\n";
		if (!passed) ++failed;
	}

	const bool oversizedRejected = !SendFrame(INVALID_SOCKET, std::string(kMaxPayloadSize + 1, 'x'));
	std::cout << (oversizedRejected ? "PASS" : "FAIL") << ": oversized frame rejected\n";
	if (!oversizedRejected) ++failed;

	const bool invalidTimeoutRejected = !SetSocketTimeouts(INVALID_SOCKET, 1000);
	std::cout << (invalidTimeoutRejected ? "PASS" : "FAIL") << ": invalid socket timeout rejected\n";
	if (!invalidTimeoutRejected) ++failed;

	std::cout << "\nSummary: " << (tests.size() + 2 - failed) << "/" << (tests.size() + 2)
		<< " passed, " << failed << " failed\n";
	return failed == 0 ? 0 : 1;
}
