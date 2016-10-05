#pragma once
// Include ALL REQUIRED Headers Here
// This file will be included in their .cpp files

// Winsock2 Headers
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <MSWSock.h>

// Cereal Serialization Headers
#include "cereal\types\string.hpp"
#include "cereal\archives\portable_binary.hpp"

// STD Headers
#include <chrono>
#include <thread>
#include <mutex>
#include <deque>
#include <forward_list>
#include <unordered_map>
#include <map>
#include <queue>

//#define _DEBUG_COMPRESSION
//#define _DEBUG_THREADS
#define _DEBUG_PACKETS
#define _DEBUG_DISCOVERY
#define _DEBUG_PACKETS_ACKS

// Core Classes
namespace PeerNet
{
	enum PacketType : unsigned char
	{
		PN_OrderedACK = 0,
		PN_ReliableACK = 1,
		PN_Ordered = 2,
		PN_Reliable = 3,
		PN_Unreliable = 4,
		PN_Discovery = 5
	};
	class NetPeer;
	class NetPacket;
}
#include "NetSocket.h"
#include "NetPacket.h"
#include "NetPeer.h"

namespace PeerNet
{
	void Initialize();
	void Deinitialize();

	NetSocket* CreateSocket(const std::string StrIP, const std::string StrPort);
	void DeleteSocket(NetSocket*const Socket);
}