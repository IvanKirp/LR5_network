#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include "Logger.h"
#include "Protocol.h"
#include "TextAnalyzer.h"

#include <atomic>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr const char* DefaultHost = "127.0.0.1";
constexpr unsigned short DefaultPort = 20054;
constexpr int ClientTimeoutMilliseconds = 10000;

std::atomic_bool serverRunning{ true };

class WinsockSession
{
public:
    WinsockSession()
        : initialized_(WSAStartup(MAKEWORD(2, 2), &data_) == 0)
    {
    }

    ~WinsockSession()
    {
        if (initialized_)
        {
            WSACleanup();
        }
    }

    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;

    bool IsInitialized() const
    {
        return initialized_;
    }

private:
    WSADATA data_{};
    bool initialized_;
};

class SocketGuard
{
public:
    explicit SocketGuard(const SOCKET socket = INVALID_SOCKET)
        : socket_(socket)
    {
    }

    ~SocketGuard()
    {
        Close();
    }

    SocketGuard(const SocketGuard&) = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;

    SOCKET Get() const
    {
        return socket_;
    }

    void Close()
    {
        if (socket_ != INVALID_SOCKET)
        {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
    }

private:
    SOCKET socket_;
};

BOOL WINAPI ConsoleHandler(const DWORD controlType)
{
    switch (controlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        serverRunning = false;
        return TRUE;
    default:
        return FALSE;
    }
}

bool ParseArguments(const int argc, char* argv[], std::string& host, unsigned short& port)
{
    host = DefaultHost;
    port = DefaultPort;

    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--host")
        {
            if (index + 1 >= argc)
            {
                std::cerr << "Missing value for --host\n";
                return false;
            }
            host = argv[++index];
        }
        else if (argument == "--port")
        {
            if (index + 1 >= argc)
            {
                std::cerr << "Missing value for --port\n";
                return false;
            }

            try
            {
                const std::string portText = argv[++index];
                std::size_t parsedLength = 0;
                const unsigned long parsedPort = std::stoul(portText, &parsedLength);
                if (parsedLength != portText.size() || parsedPort == 0 || parsedPort > 65535)
                {
                    return false;
                }
                port = static_cast<unsigned short>(parsedPort);
            }
            catch (const std::exception&)
            {
                std::cerr << "Invalid port\n";
                return false;
            }
        }
        else
        {
            std::cerr << "Unknown argument: " << argument << '\n';
            return false;
        }
    }

    return true;
}

std::string FormatStatistics(const TextStatistics& statistics)
{
    std::ostringstream result;
    result << "bytes=" << statistics.bytes << '\n'
        << "lines=" << statistics.lines << '\n'
        << "words=" << statistics.words << '\n'
        << "digits=" << statistics.digits << '\n'
        << "latinLetters=" << statistics.latinLetters << '\n';
    return result.str();
}

void HandleClient(const SOCKET clientSocket)
{
    SocketGuard client(clientSocket);
    const std::string clientName = "client socket " + std::to_string(static_cast<unsigned long long>(clientSocket));
    Log(LogLevel::Info, "Accepted " + clientName);

    if (!SetSocketTimeouts(client.Get(), ClientTimeoutMilliseconds))
    {
        Log(LogLevel::Warning, "Could not set timeouts for " + clientName);
        return;
    }

    std::string text;
    if (!RecvFrame(client.Get(), text))
    {
        Log(LogLevel::Warning, "Failed to receive frame from " + clientName + ": " + std::to_string(WSAGetLastError()));
        return;
    }

    const TextStatistics statistics = AnalyzeText(text);
    if (!SendFrame(client.Get(), FormatStatistics(statistics)))
    {
        Log(LogLevel::Warning, "Failed to send response to " + clientName + ": " + std::to_string(WSAGetLastError()));
        return;
    }

    Log(LogLevel::Info, "Processed " + clientName);
}
}

int main(int argc, char* argv[])
{
    std::string host;
    unsigned short port = DefaultPort;
    if (!ParseArguments(argc, argv, host, port))
    {
        return 2;
    }

    if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE))
    {
        Log(LogLevel::Warning, "Could not install console control handler");
    }

    WinsockSession winsock;
    if (!winsock.IsInitialized())
    {
        Log(LogLevel::Error, "WSAStartup failed: " + std::to_string(WSAGetLastError()));
        return 1;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* addressResult = nullptr;
    const std::string portText = std::to_string(port);
    const int addressStatus = getaddrinfo(host.c_str(), portText.c_str(), &hints, &addressResult);
    if (addressStatus != 0)
    {
        Log(LogLevel::Error, "getaddrinfo failed: " + std::to_string(addressStatus));
        return 1;
    }

    SocketGuard listeningSocket(socket(addressResult->ai_family, addressResult->ai_socktype, addressResult->ai_protocol));
    if (listeningSocket.Get() == INVALID_SOCKET)
    {
        Log(LogLevel::Error, "socket failed: " + std::to_string(WSAGetLastError()));
        freeaddrinfo(addressResult);
        return 1;
    }

    const int reuseAddress = 1;
    setsockopt(listeningSocket.Get(), SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress));

    if (bind(listeningSocket.Get(), addressResult->ai_addr, static_cast<int>(addressResult->ai_addrlen)) == SOCKET_ERROR)
    {
        Log(LogLevel::Error, "bind failed: " + std::to_string(WSAGetLastError()));
        freeaddrinfo(addressResult);
        return 1;
    }
    freeaddrinfo(addressResult);

    if (listen(listeningSocket.Get(), SOMAXCONN) == SOCKET_ERROR)
    {
        Log(LogLevel::Error, "listen failed: " + std::to_string(WSAGetLastError()));
        return 1;
    }

    Log(LogLevel::Info, "Server listening on " + host + ":" + std::to_string(port));
    std::vector<std::thread> clientThreads;

    while (serverRunning)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listeningSocket.Get(), &readSet);
        timeval timeout{};
        timeout.tv_sec = 1;

        const int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
        if (selectResult == SOCKET_ERROR)
        {
            Log(LogLevel::Error, "select failed: " + std::to_string(WSAGetLastError()));
            break;
        }
        if (selectResult == 0)
        {
            continue;
        }

        sockaddr_in clientAddress{};
        int clientAddressLength = sizeof(clientAddress);
        const SOCKET clientSocket = accept(listeningSocket.Get(),
            reinterpret_cast<sockaddr*>(&clientAddress), &clientAddressLength);
        if (clientSocket == INVALID_SOCKET)
        {
            if (serverRunning)
            {
                Log(LogLevel::Warning, "accept failed: " + std::to_string(WSAGetLastError()));
            }
            continue;
        }

        try
        {
            clientThreads.emplace_back(HandleClient, clientSocket);
        }
        catch (const std::exception& exception)
        {
            Log(LogLevel::Error, "Could not start client thread: " + std::string(exception.what()));
            closesocket(clientSocket);
        }
    }

    listeningSocket.Close();
    for (std::thread& clientThread : clientThreads)
    {
        if (clientThread.joinable())
        {
            clientThread.join();
        }
    }

    SetConsoleCtrlHandler(ConsoleHandler, FALSE);
    Log(LogLevel::Info, "Server stopped");
    return 0;
}