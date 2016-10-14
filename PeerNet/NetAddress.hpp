#pragma once
#include <string>

class NetAddress
{
	std::string Host;
	std::string Port;
	std::string Address;
	addrinfo*	Results;
public:
	~NetAddress() { freeaddrinfo(Results); }

	NetAddress(std::string StrHost, std::string StrPort) : Port(StrPort)
	{
		//	Describe the Hosts Protocol
		addrinfo Hint;
		ZeroMemory(&Hint, sizeof(Hint));
		Hint.ai_family = AF_INET;
		Hint.ai_socktype = SOCK_DGRAM;
		Hint.ai_protocol = IPPROTO_UDP;
		Hint.ai_flags = AI_PASSIVE;

		//	Resolve the servers addrinfo
		if (getaddrinfo(StrHost.c_str(), StrPort.c_str(), &Hint, &Results) != 0) { printf("NetAddress GetAddrInfo Failed %i\n", WSAGetLastError()); }

		//	Resolve our IP and create the formatted address
		if (Results->ai_family == AF_INET)
		{
			char*const ResolvedIP = new char[16];
			inet_ntop(AF_INET, &(((sockaddr_in*)((sockaddr*)Results->ai_addr))->sin_addr), ResolvedIP, 16);
			Host = std::string(ResolvedIP);
			delete[] ResolvedIP;
			Address = Host + std::string(":") + StrPort;
		}
		else {
			//return &(((struct sockaddr_in6*)sa)->sin6_addr);
		}
	}

	const addrinfo*const AddrInfo() const { return Results; }
	const char*const FormattedAddress() const { return Address.c_str(); }
	const sockaddr*const SockAddr() const { return Results->ai_addr; }
};