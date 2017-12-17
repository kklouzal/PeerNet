#pragma once
#include <unordered_map>
#include <string>
#include <deque>
#include <mutex>
using std::unordered_map;
using std::string;
using std::deque;
using std::mutex;

//
//
//	A single address mapped to a specific memory location inside a pool of addresses
//	Each address represents an ip address and port number
//	This can be mapped to a local network adapter or point to a remote host
struct NetAddress : public RIO_BUF
{
	string Address;
	addrinfo* Results = nullptr;

	NetAddress() : Address(), RIO_BUF() {}

	//	Resolve initializes the NetAddress from an IP address or hostname along with a port number
	void Resolve(string StrHost, string StrPort)
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
			Address = string(ResolvedIP) + string(":") + StrPort;
			delete[] ResolvedIP;
		}
		else {
			//return &(((struct sockaddr_in6*)sa)->sin6_addr);
		}
	}

	~NetAddress() { freeaddrinfo(Results); }

	const char*const FormattedAddress() const { return Address.c_str(); }
	const addrinfo*const AddrInfo() const { return Results; }
};

//
//
//	AddressPool
//	Manages a pool of addresses in memory
//	Which are handed out to objects of type T
//	Supporting a maximum of MaxObjects addresses
template <typename T>
class AddressPool
{
	mutex AddrMutex;
	//unordered_map<SOCKADDR_INET, T> Objects;
	unordered_map<string, T> Objects;
	deque<NetAddress*> UsedAddr;
	deque<NetAddress*> UnusedAddr;
	RIO_BUFFERID Addr_BufferID;
	PCHAR Addr_Buffer;

public:

	AddressPool(RIO_EXTENSION_FUNCTION_TABLE &RIO, unsigned int MaxObjects) :
		AddrMutex(), Objects(), Addr_BufferID(), Addr_Buffer(new char[MaxObjects * sizeof(SOCKADDR_INET)]), UsedAddr(), UnusedAddr()
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
				NetAddress* Address = new NetAddress();
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

	T GetExisting(SOCKADDR_INET* AddrBuff)
	{
		//	Check if we already have a connected object with this address
		AddrMutex.lock();
		const string Formatted = inet_ntoa(AddrBuff->Ipv4.sin_addr) + string(":") + std::to_string(ntohs(AddrBuff->Ipv4.sin_port));
		//if (Objects.count(AddrBuff))
		if (Objects.count(Formatted))
		{
			//T ThisObject = Objects.at(AddrBuff);
			T ThisObject = Objects.at(Formatted);
			AddrMutex.unlock();
			return ThisObject;	//	Already have a connected object for this ip/port
		}
		AddrMutex.unlock();
		return nullptr;	//	No connected object exists
	}

	NetAddress* FreeAddress()
	{
		AddrMutex.lock();
		if (UnusedAddr.empty()) { AddrMutex.unlock(); return nullptr; }

		NetAddress* NewAddress = UnusedAddr.back();
		UsedAddr.push_front(UnusedAddr.back());
		UnusedAddr.pop_back();
		AddrMutex.unlock();
		return NewAddress;
	}

	const bool New(string StrIP, string StrPort, T& ExistingObj, NetAddress*& NewAddr)
	{
		AddrMutex.lock();
		if (UnusedAddr.empty()) { AddrMutex.unlock(); return false; }	//	No available objects to hand out

		NewAddr = UnusedAddr.back();
		UsedAddr.push_front(UnusedAddr.back());
		UnusedAddr.pop_back();

		//	resolve our Address from the supplied IP and Port
		NewAddr->Resolve(StrIP, StrPort);

		std::memcpy(&Addr_Buffer[NewAddr->Offset], NewAddr->AddrInfo()->ai_addr, sizeof(SOCKADDR_INET));

		//	Check if we already have a connected object with this address
		//if (Objects.count((SOCKADDR_INET*)NewAddr->AddrInfo()->ai_addr))
		if (Objects.count(NewAddr->FormattedAddress()))
		{
			//ExistingObj = Objects.at((SOCKADDR_INET*)NewAddr->AddrInfo()->ai_addr);
			ExistingObj = Objects.at(NewAddr->FormattedAddress());
			AddrMutex.unlock();
			return false;	//	Already have a connected object for this ip/port
		}
		AddrMutex.unlock();
		return true;	//	Go ahead and create a new object
	}

	void InsertConnected(NetAddress* Address, T NewObject)
	{
		AddrMutex.lock();
		//Objects.emplace((SOCKADDR_INET*)Address->AddrInfo()->ai_addr, NewObject);
		Objects.emplace(Address->FormattedAddress(), NewObject);
		AddrMutex.unlock();
	}
};