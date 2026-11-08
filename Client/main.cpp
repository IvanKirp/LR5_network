#include <winsock2.h>
#include <ws2tcpip.h>

#include "Protocol.h"

#include <fstream>
#include <iostream>
#include <string>
#include <utility>

namespace
{
	constexpr const char* DefaultHost = "127.0.0.1";
	constexpr unsigned short DefaultPort = 20054;
	constexpr int TimeoutMilliseconds = 10000;

	class WinsockSession
	{
	public:
		WinsockSession() : initialized_(WSAStartup(MAKEWORD(2, 2), &data_) == 0) {}
		~WinsockSession() { if (initialized_) WSACleanup(); }
		bool IsInitialized() const { return initialized_; }
	private:
		WSADATA data_{};
		bool initialized_;
	};

	class SocketGuard
	{
	public:
		explicit SocketGuard(SOCKET value = INVALID_SOCKET) : value_(value) {}
		~SocketGuard() { Close(); }
		SocketGuard(const SocketGuard&) = delete;
		SocketGuard& operator=(const SocketGuard&) = delete;
		SocketGuard(SocketGuard&& other) noexcept : value_(other.value_) { other.value_ = INVALID_SOCKET; }
		SocketGuard& operator=(SocketGuard&& other) noexcept
		{
			if (this != &other) { Close(); value_ = other.value_; other.value_ = INVALID_SOCKET; }
			return *this;
		}
		SOCKET Get() const { return value_; }
		void Close() { if (value_ != INVALID_SOCKET) { closesocket(value_); value_ = INVALID_SOCKET; } }
	private:
		SOCKET value_;
	};

	bool ParseArguments(int argc, char* argv[], std::string& host, unsigned short& port, std::string& text)
	{
		host = DefaultHost;
		port = DefaultPort;
		std::string filePath;
		bool hasText = false;
		bool hasFile = false;

		for (int index = 1; index < argc; ++index)
		{
			const std::string option = argv[index];
			if (index + 1 >= argc) return false;
			const std::string value = argv[++index];
			if (option == "--host") host = value;
			else if (option == "--port")
			{
				try
				{
					std::size_t parsed = 0;
					const unsigned long number = std::stoul(value, &parsed);
					if (parsed != value.size() || number == 0 || number > 65535) return false;
					port = static_cast<unsigned short>(number);
				}
				catch (...) { return false; }
			}
			else if (option == "--text") { text = value; hasText = true; }
			else if (option == "--file") { filePath = value; hasFile = true; }
			else return false;
		}

		if (hasText == hasFile) return false;
		if (hasFile)
		{
			std::ifstream input(filePath, std::ios::binary);
			if (!input) return false;
			text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		}
		return text.size() <= kMaxPayloadSize;
	}
}

int main(int argc, char* argv[])
{
	std::string host;
	unsigned short port = DefaultPort;
	std::string text;
	if (!ParseArguments(argc, argv, host, port, text))
	{
		std::cerr << "Usage: Client.exe --host HOST --port PORT (--text TEXT | --file PATH)\n";
		return 2;
	}

	WinsockSession winsock;
	if (!winsock.IsInitialized()) return 1;

	addrinfo hints{};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	addrinfo* addresses = nullptr;
	const std::string service = std::to_string(port);
	if (getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0) return 1;

	SocketGuard connection;
	for (addrinfo* address = addresses; address != nullptr; address = address->ai_next)
	{
		SocketGuard candidate(socket(address->ai_family, address->ai_socktype, address->ai_protocol));
		if (candidate.Get() != INVALID_SOCKET && connect(candidate.Get(), address->ai_addr, static_cast<int>(address->ai_addrlen)) != SOCKET_ERROR)
		{
			connection = std::move(candidate);
			break;
		}
	}
	freeaddrinfo(addresses);

	if (connection.Get() == INVALID_SOCKET || !SetSocketTimeouts(connection.Get(), TimeoutMilliseconds) || !SendFrame(connection.Get(), text)) return 1;

	std::string response;
	if (!RecvFrame(connection.Get(), response)) return 1;
	std::cout << response;
	return 0;
}
