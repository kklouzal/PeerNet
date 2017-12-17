#pragma once
// Include ALL REQUIRED Headers Here
// This file will be included in their .cpp files

// Winsock2 Headers
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <MSWSock.h>

// Cereal Serialization Headers
#include "cereal\types\string.hpp"
#include "cereal\archives\binary.hpp"
#include "cereal\archives\portable_binary.hpp"

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

enum
{
	PN_LoopBackPort = 9999,

	PN_MaxPacketSize = 1472,		//	Max size of an outgoing or incoming packet
	RIO_ResultsPerThread = 128,		//	How many results to dequeue from the stack per thread
	PN_MaxSendPackets = 14336,		//	Max outgoing packets per socket before you run out of memory
	PN_MaxReceivePackets = 14336	//	Max pending incoming packets before new packets are disgarded
};

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
	class NetPacket;
}

#include "NetAddress.hpp"
#include "NetPacket.h"
#include "NetSocket.h"
#include "NetPeer.h"

namespace PeerNet
{
	class PeerNet
	{
	private:
		static PeerNet* _instance;

		RIO_EXTENSION_FUNCTION_TABLE g_rio;

		AddressPool<NetPeer*>* PeerKeeper = nullptr;
		AddressPool<NetSocket*>* SocketKeeper = nullptr;

		PeerNet(unsigned int MaxPeers, unsigned int MaxSockets) {
			printf("Initializing PeerNet\n");
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

				//	Create the Address Pool handlers
				PeerKeeper = new AddressPool<NetPeer*>(g_rio, MaxPeers);
				SocketKeeper = new AddressPool<NetSocket*>(g_rio, MaxSockets);

				//	TODO: Initialize our send/receive packets
				printf("Initialization Complete\n");
			}
		}

		~PeerNet()
		{
			printf("Deinitializing PeerNet\n");

			WSACleanup();
			delete PeerKeeper;
			delete SocketKeeper;
			printf("Deinitialization Complete\n");
		}

	public:

		//	Initialize PeerNet
		static PeerNet* Initialize(unsigned int MaxPeers, unsigned int MaxSockets)
		{
			if (_instance == nullptr)
			{
				_instance = new PeerNet(MaxPeers, MaxSockets);
			}
			return _instance;
		}

		//	Deinitialize PeerNet
		static void Deinitialize()
		{
			if (_instance != nullptr)
			{
				delete _instance;
				_instance = nullptr;
			}
		}

		static PeerNet* getInstance()
		{
			return _instance;
		}

		//	Returns access to the RIO Function Table
		RIO_EXTENSION_FUNCTION_TABLE RIO()
		{
			return g_rio;
		}

		//	Creates a socket and starts listening at the specified IP and Port
		//	Returns socket if it already exists
		NetSocket*const OpenSocket(string StrIP, string StrPort)
		{
			NetSocket* ExistingSocket = NULL;
			NetAddress* NewAddress;
			//	Can we create a new NetSocket with this ip/port?
			if (SocketKeeper->New(StrIP, StrPort, ExistingSocket, NewAddress))
			{
				NetSocket* ThisSocket = new NetSocket(NewAddress);
				SocketKeeper->InsertConnected(NewAddress, ThisSocket);
				return ThisSocket;
			}
			//	No available connections or object already connected
			return ExistingSocket;
		}

		//	Creates and connects to a peer at the specified IP and Port
		//	Returns peer if it already exists
		//
		//	ToDo: The NetAddress created through NewAddress needs to be returned to PeerKeeper's Unused container
		//	when this NetPeer is cleaned up
		NetPeer*const ConnectPeer(string StrIP, string StrPort, NetSocket* DefaultSocket)
		{
			if (DefaultSocket == nullptr) { printf("Error: DefaultSocket NULL\n"); return nullptr; }
			NetPeer* ExistingPeer = nullptr;
			NetAddress* NewAddress = nullptr;
			//	Can we create a new NetPeer with this ip/port?
			if (PeerKeeper->New(StrIP, StrPort, ExistingPeer, NewAddress))
			{
				NetPeer* ThisPeer = new NetPeer(DefaultSocket, NewAddress);
				PeerKeeper->InsertConnected(NewAddress, ThisPeer);
				return ThisPeer;
			}
			//	No available connections or object already connected
			return ExistingPeer;
		}

		//	Need DisconnectPeer/CloseSocket to properly cleanup our internal containers
		//	Or split those functions up into their respective files
		//	And let their respective classes destructors handle it <--
		void DisconnectPeer(SOCKADDR_INET* AddrBuff)
		{

		}

		//	Checks for an existing connected peer and returns it
		//	Or returns a newly constructed NetPeer and immediatly sends the discovery packet
		NetPeer*const GetPeer(SOCKADDR_INET* AddrBuff, NetSocket* DefaultSocket)
		{
			//	See if we already have an existing peer
			NetPeer* ThisPeer = PeerKeeper->GetExisting(AddrBuff);
			if (ThisPeer != nullptr) { return ThisPeer; }

			const string SenderIP(inet_ntoa(AddrBuff->Ipv4.sin_addr));
			const string SenderPort(std::to_string(ntohs(AddrBuff->Ipv4.sin_port)));

			return ConnectPeer(SenderIP, SenderPort, DefaultSocket);
		}
	};
}