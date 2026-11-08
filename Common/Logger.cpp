#include "pch.h"
#include "Logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace
{
	std::mutex logMutex;

	const char* ToString(const LogLevel level)
	{
		switch (level)
		{
		case LogLevel::Info:
			return "INFO";
		case LogLevel::Warning:
			return "WARNING";
		case LogLevel::Error:
			return "ERROR";
		default:
			return "UNKNOWN";
		}
	}
}

void Log(const LogLevel level, const std::string& message)
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t time = std::chrono::system_clock::to_time_t(now);
	std::tm localTime{};
	localtime_s(&localTime, &time);

	std::lock_guard<std::mutex> lock(logMutex);
	std::cerr << '[' << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << "] "
		<< '[' << ToString(level) << "] " << message << '\n';
}
