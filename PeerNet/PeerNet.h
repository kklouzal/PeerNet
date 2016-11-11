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
#include "cereal\archives\portable_binary.hpp"

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

#define LOOPBACK_PORT 9999
#define MaxPeers 1024
#define MaxSockets 32

// Core Classes
namespace PeerNet
{
	enum PacketType : unsigned short
	{
		PN_KeepAlive = 0,
		PN_Ordered = 1,
		PN_Reliable = 2,
		PN_Unreliable = 3
	};
	class NetPeer;
	class NetSocket;
	class NetPacket;

	//	Returns access to the RIO Function Table
	RIO_EXTENSION_FUNCTION_TABLE RIO();
}

#include "NetAddress.hpp"
#include "NetPacket.h"
#include "NetSocket.h"
#include "NetPeer.h"

namespace PeerNet
{
	//	Initialize PeerNet
	void Initialize();
	//	Deinitialize PeerNet
	void Deinitialize();

	//	Creates a socket and starts listening at the specified IP and Port
	//	Returns socket if it already exists
	NetSocket*const OpenSocket(string StrIP, string StrPort);

	//	Creates and connects to a peer at the specified IP and Port
	//	Returns peer if it already exists
	NetPeer* const ConnectPeer(std::string StrIP, std::string StrPort, NetSocket* DefaultSocket);

	//	Checks for an existing connected peer and returns it
	//	Or returns a newly constructed NetPeer and immediatly sends the discovery packet
	NetPeer*const GetPeer(SOCKADDR_INET* AddrBuff, NetSocket* DefaultSocket);
}