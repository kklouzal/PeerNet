#pragma once
// Include ALL REQUIRED Headers Here
// This file will be included in their .cpp files

// Winsock2 Headers
#define WIN32_LEAN_AND_MEAN
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
#define _DEBUG_DISCOVERY
#define _DEBUG_PACKETS_ORDERED
//#define _DEBUG_PACKETS_RELIABLE
#define _DEBUG_PACKETS_UNRELIABLE
#define _DEBUG_PACKETS_RELIABLE_ACK
#define _DEBUG_PACKETS_ORDERED_ACK

// Core Classes
namespace PeerNet
{
	enum PacketType : unsigned char
	{
		PN_OrderedACK = 0,
		PN_ReliableACK = 1,
		PN_Ordered = 2,
		PN_Reliable = 3,
		PN_Unreliable = 4
	};
	class NetPeer;
	class NetSocket;
}

#include "NetAddress.hpp"
#include "NetPacket.h"
#include "NetSocket.h"
#include "NetPeer.h"

namespace PeerNet
{
	void Initialize();
	void Deinitialize();
}