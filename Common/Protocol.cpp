#include "pch.h"
#include "Protocol.h"

#include <limits>

bool SendAll(SOCKET socket, const char* data, const int length)
{
	if (length < 0 || (length > 0 && data == nullptr))
	{
		WSASetLastError(WSAEINVAL);
		return false;
	}

	int totalSent = 0;
	while (totalSent < length)
	{
		const int sent = send(socket, data + totalSent, length - totalSent, 0);
		if (sent == SOCKET_ERROR || sent == 0)
		{
			return false;
		}

		totalSent += sent;
	}

	return true;
}

bool RecvAll(SOCKET socket, char* data, const int length)
{
	if (length < 0 || (length > 0 && data == nullptr))
	{
		WSASetLastError(WSAEINVAL);
		return false;
	}

	int totalReceived = 0;
	while (totalReceived < length)
	{
		const int received = recv(socket, data + totalReceived, length - totalReceived, 0);
		if (received == SOCKET_ERROR || received == 0)
		{
			return false;
		}

		totalReceived += received;
	}

	return true;
}

bool SendFrame(SOCKET socket, const std::string& payload)
{
	if (payload.size() > kMaxPayloadSize ||
		payload.size() > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()))
	{
		WSASetLastError(WSAEMSGSIZE);
		return false;
	}

	const auto payloadLength = static_cast<std::uint32_t>(payload.size());
	const std::uint32_t networkLength = htonl(payloadLength);

	if (!SendAll(socket, reinterpret_cast<const char*>(&networkLength), sizeof(networkLength)))
	{
		return false;
	}

	return SendAll(socket, payload.data(), static_cast<int>(payload.size()));
}

bool RecvFrame(SOCKET socket, std::string& payload)
{
	std::uint32_t networkLength = 0;
	if (!RecvAll(socket, reinterpret_cast<char*>(&networkLength), sizeof(networkLength)))
	{
		return false;
	}

	const std::uint32_t payloadLength = ntohl(networkLength);
	if (payloadLength > kMaxPayloadSize)
	{
		WSASetLastError(WSAEMSGSIZE);
		return false;
	}

	payload.resize(payloadLength);
	if (payloadLength == 0)
	{
		return true;
	}

	return RecvAll(socket, payload.data(), static_cast<int>(payloadLength));
}

bool SetSocketTimeouts(SOCKET socket, const int timeoutMilliseconds)
{
	if (timeoutMilliseconds <= 0)
	{
		WSASetLastError(WSAEINVAL);
		return false;
	}

	if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
		reinterpret_cast<const char*>(&timeoutMilliseconds), sizeof(timeoutMilliseconds)) == SOCKET_ERROR)
	{
		return false;
	}

	if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
		reinterpret_cast<const char*>(&timeoutMilliseconds), sizeof(timeoutMilliseconds)) == SOCKET_ERROR)
	{
		return false;
	}

	return true;
}
