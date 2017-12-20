#pragma once
#include <string>
#include <deque>
#include <mutex>

//
//
//	A single address mapped to a specific memory location inside a pool of addresses
//	Each address represents an ip address and port number
//	This can be mapped to a local network adapter or point to a remote host
struct NetAddress : public RIO_BUF
{
	std::string Address;
	addrinfo* Results = nullptr;

	NetAddress() : Address(), RIO_BUF() {}

	//	Resolve initializes the NetAddress from an IP address or hostname along with a port number
	void Resolve(std::string StrHost, std::string StrPort)
	{
		//	Describe the End Hosts Protocol
		addrinfo Hint;
		ZeroMemory(&Hint, sizeof(Hint));
		Hint.ai_family = AF_INET;
		Hint.ai_socktype = SOCK_DGRAM;
		Hint.ai_protocol = IPPROTO_UDP;
		Hint.ai_flags = AI_PASSIVE;
		//	Resolve the End Hosts addrinfo
		if (getaddrinfo(StrHost.c_str(), StrPort.c_str(), &Hint, &Results) != 0) { printf("NetAddress GetAddrInfo Failed %i\n", WSAGetLastError()); }

		//	Create our formatted address
		if (Results->ai_family == AF_INET)
		{
			char*const ResolvedIP = new char[16];
			inet_ntop(AF_INET, &(((sockaddr_in*)((sockaddr*)Results->ai_addr))->sin_addr), ResolvedIP, 16);
			Address = std::string(ResolvedIP) + std::string(":") + StrPort;
			delete[] ResolvedIP;
		}
		else {
			//return &(((struct sockaddr_in6*)sa)->sin6_addr);
		}
	}

	~NetAddress() { freeaddrinfo(Results); }

	inline const std::string& GetFormatted() const { return Address; }
	//get rid of this next one!
	const char*const FormattedAddress() const { return Address.c_str(); }
	const addrinfo*const AddrInfo() const { return Results; }
};

//
//
//	AddressPool
//	Manages a pool of addresses in memory
//	Supporting a maximum of MaxObjects addresses
class AddressPool
{
	std::mutex AddrMutex;
	std::deque<NetAddress*> UsedAddr;
	std::deque<NetAddress*> UnusedAddr;
	RIO_BUFFERID Addr_BufferID;
	PCHAR Addr_Buffer;

public:

	AddressPool(RIO_EXTENSION_FUNCTION_TABLE &RIO, unsigned int MaxObjects) :
		AddrMutex(), Addr_BufferID(), Addr_Buffer(new char[MaxObjects * sizeof(SOCKADDR_INET)]), UsedAddr(), UnusedAddr()
	{
		//	Initialize Address Memory Buffer
		printf("Address Buffer: ");
		Addr_BufferID = RIO.RIORegisterBuffer(Addr_Buffer, sizeof(SOCKADDR_INET)*MaxObjects);
		if (Addr_BufferID == RIO_INVALID_BUFFERID)
		{
			printf("Invalid Memory BufferID\n");
		}
		else {
			for (DWORD i = 0, AddressOffset = 0; i < MaxObjects; i++/*, AddressOffset += sizeof(SOCKADDR_INET)*/)
			{
				NetAddress*const Address = new NetAddress();
				Address->BufferId = Addr_BufferID;
				Address->Offset = AddressOffset;
				Address->Length = sizeof(SOCKADDR_INET);
				UnusedAddr.push_front(Address);

				AddressOffset += sizeof(SOCKADDR_INET);
			}
			printf("%zu\n", UnusedAddr.size());
		}
	}

	~AddressPool()
	{
		delete[] Addr_Buffer;
		while (!UnusedAddr.empty())
		{
			NetAddress* Addr = UnusedAddr.front();
			delete Addr;
			UnusedAddr.pop_front();
		}
	}

	//	Must be called after ->Resolve to write the resolved data to the address buffer
	void WriteAddress(NetAddress*const Addr)
	{
		std::memcpy(&Addr_Buffer[Addr->Offset], Addr->AddrInfo()->ai_addr, sizeof(SOCKADDR_INET));
	}

	//	Returns a free and empty address
	NetAddress*const FreeAddress()
	{
		AddrMutex.lock();
		if (UnusedAddr.empty()) { AddrMutex.unlock(); return nullptr; }

		NetAddress*const NewAddress = UnusedAddr.back();
		UsedAddr.push_front(UnusedAddr.back());
		UnusedAddr.pop_back();
		AddrMutex.unlock();
		return NewAddress;
	}

	//	Returns a free address from an existing SOCKADDR_INET
	NetAddress*const FreeAddress(SOCKADDR_INET*const AddrBuff)
	{
		AddrMutex.lock();
		if (UnusedAddr.empty()) { AddrMutex.unlock(); return nullptr; }

		NetAddress*const NewAddress = UnusedAddr.back();
		UsedAddr.push_front(UnusedAddr.back());
		UnusedAddr.pop_back();
		AddrMutex.unlock();
		std::memcpy(&Addr_Buffer[NewAddress->Offset], AddrBuff, sizeof(SOCKADDR_INET));
		return NewAddress;
	}
};