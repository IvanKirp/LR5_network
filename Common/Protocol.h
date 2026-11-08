#pragma once

#include <winsock2.h>

#include <cstddef>
#include <cstdint>
#include <string>

inline constexpr std::uint32_t kMaxPayloadSize = 65536;

bool SendAll(SOCKET socket, const char* data, int length);
bool RecvAll(SOCKET socket, char* data, int length);
bool SendFrame(SOCKET socket, const std::string& payload);
bool RecvFrame(SOCKET socket, std::string& payload);
bool SetSocketTimeouts(SOCKET socket, int timeoutMilliseconds);
