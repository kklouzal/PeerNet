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
#include "zstd\zstd.h"

// STD Headers
#include <chrono>
#include <mutex>
#include <queue>
#include <unordered_map>


//	Performance Tuning
//#define _PERF_SPINLOCK	//	Higher CPU Usage for more responsive packet handling; lower latencies

#define LOOPBACK_PORT 9999
#define MaxPeers 1024
#define MaxSockets 32

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

	//	Returns access to the RIO Function Table
	RIO_EXTENSION_FUNCTION_TABLE RIO();
}

#include "Logger.hpp"
namespace PeerNet
{
	void Log(std::string strLog);
}

#include "NetAddress.hpp"
#include "NetPacket.h"
#include "NetSocket.h"
#include "NetPeer.hpp"

namespace PeerNet
{
	//	Initialize PeerNet
	void Initialize(Logger* LoggingClass);
	//	Deinitialize PeerNet
	void Deinitialize();

	//	Creates a socket and starts listening at the specified IP and Port
	//	Returns socket if it already exists
	NetSocket*const OpenSocket(std::string StrIP, std::string StrPort);

	//	Creates and connects to a peer at the specified IP and Port
	//	Returns peer if it already exists
	NetPeer* const ConnectPeer(std::string StrIP, std::string StrPort, NetSocket* DefaultSocket);

	//	Checks for an existing connected peer and returns it
	//	Or returns a newly constructed NetPeer and immediatly sends the discovery packet
	NetPeer*const GetPeer(SOCKADDR_INET* AddrBuff, NetSocket* DefaultSocket);

	NetSocket*const LoopBack();
	NetPeer*const LocalHost();
}