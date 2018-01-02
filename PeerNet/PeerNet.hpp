#pragma once
// Include ALL REQUIRED Headers Here
// This file will be included in their .cpp files

// Winsock2 Headers
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <MSWSock.h>
#pragma comment(lib, "ws2_32.lib")

// Cereal Serialization Headers
#include <cereal\types\string.hpp>
#include <cereal\types\chrono.hpp>
#include <cereal\archives\binary.hpp>
#include <cereal\archives\portable_binary.hpp>

//	Compression Headers
#include <zstd.h>

// STD Headers
#include <chrono>
#include <mutex>
#include <queue>
#include <unordered_map>

//#define _DEBUG_COMPRESSION
//#define _DEBUG_THREADS
#define _DEBUG_DISCOVERY
//#define _DEBUG_PACKETS_ORDERED
//#define _DEBUG_PACKETS_RELIABLE
//#define _DEBUG_PACKETS_UNRELIABLE
//#define _DEBUG_PACKETS_RELIABLE_ACK
//#define _DEBUG_PACKETS_ORDERED_ACK

//	Performance Tuning
//#define _PERF_SPINLOCK	//	Higher CPU Usage for more responsive packet handling; lower latencies


// Core Classes
namespace PeerNet
{
	enum PacketType : unsigned short
	{
		PN_KeepAlive = 0,
		PN_Ordered = 1,
		PN_Reliable = 2,
		PN_Unreliable = 3,
		PN_NotInialized = 1001
	};
	class NetPeer;
	class NetSocket;
	class NetPeerFactory;
}

#include "NetAddress.hpp"
#include "NetPacket.hpp"

namespace PeerNet
{
	class PeerNet
	{
	private:
		RIO_EXTENSION_FUNCTION_TABLE g_rio;

		AddressPool* Addresses = nullptr;

		std::unordered_map<string, NetSocket*const> Sockets;
		std::unordered_map<string, NetPeer*const> Peers;

		std::mutex SocketMutex;
		std::mutex PeerMutex;

		NetSocket* DefaultSocket = nullptr;
		NetPeerFactory* _PeerFactory;

	public:

		inline PeerNet(NetPeerFactory* PeerFactory, unsigned int MaxPeers, unsigned int MaxSockets);
		inline ~PeerNet();

		//	Sets the default socket used by new peers
		inline void SetDefaultSocket(NetSocket* Socket) { DefaultSocket = Socket; }

		//	Returns access to the RIO Function Table
		inline RIO_EXTENSION_FUNCTION_TABLE& RIO() { return g_rio; }

		//	Creates a socket and starts listening at the specified IP and Port
		//	Returns socket if it already exists
		inline NetSocket*const OpenSocket(string IP, string Port);

		//	Need DisconnectPeer/CloseSocket to properly cleanup our internal containers
		//	Or split those functions up into their respective files
		//	And let their respective classes destructors handle it <--
		inline void DisconnectPeer(NetPeer*const Peer);

		//	Takes raw incoming uncompressed data and an address buffer
		//	Gets a peer from the buffer and passes the data to them for processing
		inline void TranslateData(const SOCKADDR_INET*const AddrBuff, const string& IncomingData);

		//	Gets an existing peer from a provided AddrBuff
		//	Creates a new peer if one does not exist
		inline NetPeer*const PeerNet::GetPeer(const SOCKADDR_INET*const AddrBuff);
		inline NetPeer*const GetPeer(string IP, string Port);
	};
}

#include "NetSocket.hpp"
#include "NetPeer.hpp"

namespace PeerNet
{
	//	Base NetPeer Factory Class
	//	Users can provide their own peer class as long as it inherits from NetPeer
	class NetPeerFactory
	{
	public:
		inline virtual NetPeer* Create(PeerNet* PNInstance, NetSocket*const DefaultSocket, NetAddress*const NetAddr) = 0;
	};


	inline PeerNet::PeerNet(NetPeerFactory* PeerFactory, unsigned int MaxPeers, unsigned int MaxSockets)
		: _PeerFactory(PeerFactory) {
		printf("Initializing PeerNet\n");
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
		//	Startup WinSock 2.2
		const size_t iResult = WSAStartup(MAKEWORD(2, 2), &WSADATA());
		if (iResult != 0) {
			printf("\tWSAStartup Error: %i\n", (int)iResult);
		}
		else {
			//	Create a dummy socket long enough to get our RIO Function Table pointer
			SOCKET RioSocket = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL, WSA_FLAG_REGISTERED_IO);
			GUID functionTableID = WSAID_MULTIPLE_RIO;
			DWORD dwBytes = 0;
			if (WSAIoctl(RioSocket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
				&functionTableID,
				sizeof(GUID),
				(void**)&g_rio,
				sizeof(g_rio),
				&dwBytes, 0, 0) == SOCKET_ERROR) {
				printf("RIO Failed(%i)\n", WSAGetLastError());
			}
			closesocket(RioSocket);

			//	Create the Address Pool
			Addresses = new AddressPool(g_rio, MaxPeers + MaxSockets);

			//SetDefaultSocket(OpenSocket("127.0.0.1", "9999"));
			//	TODO: Initialize our send/receive packets
			printf("Initialization Complete\n");
		}
	}
	inline PeerNet::~PeerNet()
	{
		printf("Deinitializing PeerNet\n");
		for (auto Peer : Peers) {
			delete Peer.second;
		}
		for (auto Socket : Sockets) {
			delete Socket.second;
		}
		WSACleanup();
		delete Addresses;
		printf("Deinitialization Complete\n");
	}
	inline void PeerNet::TranslateData(const SOCKADDR_INET*const AddrBuff, const string& IncomingData)
	{
		GetPeer(AddrBuff)->Receive_Packet(IncomingData);
	}
	inline void PeerNet::DisconnectPeer(NetPeer*const Peer)
	{
		auto it = Peers.find(Peer->GetAddress()->GetFormatted());
		if (it != Peers.end())
		{
#ifdef _PERF_SPINLOCK
			while (!PeerMutex.try_lock()) {}
#else
			PeerMutex.lock();
#endif
			Peers.erase(it);
			PeerMutex.unlock();
			delete Peer;
		}
	}
	inline NetPeer*const PeerNet::GetPeer(const SOCKADDR_INET*const AddrBuff)
	{
		//	Check if we already have a connected object with this address
		//const string Formatted(IP + string(":") + Port);
		const string Formatted(inet_ntoa(AddrBuff->Ipv4.sin_addr) + std::string(":") + std::to_string(ntohs(AddrBuff->Ipv4.sin_port)));
		auto it = Peers.find(Formatted);
		if (it != Peers.end())
		{
			return it->second;	//	Already have a connected object for this ip/port
		}
		else {
			//NetAddress* NewAddr = Addresses->FreeAddress(AddrBuff);
			NetAddress*const NewAddr = Addresses->FreeAddress();
			NewAddr->Resolve(string(inet_ntoa(AddrBuff->Ipv4.sin_addr)), string(std::to_string(ntohs(AddrBuff->Ipv4.sin_port))));
			Addresses->WriteAddress(NewAddr);
			NetPeer*const ThisPeer = _PeerFactory->Create(this, DefaultSocket, NewAddr);
#ifdef _PERF_SPINLOCK
			while (!PeerMutex.try_lock()) {}
#else
			PeerMutex.lock();
#endif
			Peers.emplace(Formatted, ThisPeer);
			PeerMutex.unlock();
			return ThisPeer;
		}
	}
	inline NetPeer*const PeerNet::GetPeer(string IP, string Port)
	{
		//	Check if we already have a connected object with this address
		const string Formatted(IP + string(":") + Port);
		auto it = Peers.find(Formatted);
		if (it != Peers.end())
		{
			return it->second;	//	Already have a connected object for this ip/port
		}
		else {
			NetAddress*const NewAddr = Addresses->FreeAddress();
			NewAddr->Resolve(IP, Port);
			Addresses->WriteAddress(NewAddr);
			NetPeer*const ThisPeer = _PeerFactory->Create(this, DefaultSocket, NewAddr);
#ifdef _PERF_SPINLOCK
			while (!PeerMutex.try_lock()) {}
#else
			PeerMutex.lock();
#endif
			Peers.emplace(Formatted, ThisPeer);
			PeerMutex.unlock();
			return ThisPeer;
		}
	}
	//	Creates a socket and starts listening at the specified IP and Port
	//	Returns socket if it already exists
	inline NetSocket*const PeerNet::OpenSocket(string IP, string Port)
	{
		//	Check if we already have a connected object with this address
		const string Formatted(IP + string(":") + Port);
		auto it = Sockets.find(Formatted);
		if (it != Sockets.end())
		{
			return it->second;	//	Already have a connected object for this ip/port
		}
		else {
			NetAddress*const NewAddr = Addresses->FreeAddress();
			NewAddr->Resolve(IP, Port);
			NetSocket*const ThisSocket = new NetSocket(this, NewAddr);
#ifdef _PERF_SPINLOCK
			while (!SocketMutex.try_lock()) {}
#else
			SocketMutex.lock();
#endif
			Sockets.emplace(Formatted, ThisSocket);
			SocketMutex.unlock();
			return ThisSocket;
		}
	}
}