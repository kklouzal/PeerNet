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
	addrinfo* Results;

	NetAddress() : RIO_BUF() {}

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
template <typename T, unsigned long long MaxObjects>
class AddressPool
{

public:
	mutex AddrMutex;
	unordered_map<PCHAR, T> Objects;
	deque<NetAddress*> UsedAddr;
	deque<NetAddress*> UnusedAddr;
	RIO_BUFFERID Addr_BufferID;
	PCHAR Addr_Buffer;

	AddressPool() :
		AddrMutex(), Objects(), Addr_BufferID(), Addr_Buffer(new char[sizeof(SOCKADDR_INET)*MaxObjects]), UsedAddr(), UnusedAddr()
	{}

	~AddressPool() { delete[] Addr_Buffer; }

	T GetExisting(PCHAR AddrBuff)
	{
		//	Check if we already have a connected object with this address
		AddrMutex.lock();
		if (Objects.count(AddrBuff))
		{
			T ThisObject = Objects.at(AddrBuff);
			AddrMutex.unlock();
			return ThisObject;	//	Already have a connected object for this ip/port
		}
		return nullptr;	//	No connected object exists
	}

	const bool New(string StrIP, string StrPort, T ExistingObj, NetAddress* NewAddr)
	{
		ExistingObj = nullptr;
		NewAddr = nullptr;
		AddrMutex.lock();
		if (UnusedAddr.empty()) { AddrMutex.unlock(); return false; }	//	No available objects to hand out

		NetAddress* NewAddress = UnusedAddr.back();
		UsedAddr.push_front(UnusedAddr.back());
		UnusedAddr.pop_back();

		//	resolve our Address from the supplied IP and Port
		NewAddress->Resolve(StrIP, StrPort);

		std::memcpy(&Addr_Buffer[NewAddress->Offset], SendPacket->GetPeer()->SockAddr(), sizeof(SOCKADDR_INET));


		//	Check if we already have a connected object with this address
		if (Objects.count(&Addr_Buffer[NewAddress->Offset]))
		{
			T ThisObject = Objects.at(&Addr_Buffer[NewAddress->Offset]);
			AddrMutex.unlock();
			ExistingObj = ThisObject;
			return false;	//	Already have a connected object for this ip/port
		}
		return true;	//	Go ahead and create a new object
	}

	void InsertConnected(NetAddress* Address, T NewObject)
	{
		AddrMutex.lock();
		Objects.emplace(&Addr_Buffer[Address->Offset], NewObject);
		AddrMutex.unlock();
	}
};