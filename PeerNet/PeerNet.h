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
#include <mutex>
#include <queue>
#include <unordered_map>

//#define _DEBUG_COMPRESSION
//#define _DEBUG_THREADS
#define _DEBUG_DISCOVERY
#define _DEBUG_PACKETS_ORDERED
//#define _DEBUG_PACKETS_RELIABLE
#define _DEBUG_PACKETS_UNRELIABLE
//#define _DEBUG_PACKETS_RELIABLE_ACK
//#define _DEBUG_PACKETS_ORDERED_ACK

//	Performance Tuning
//#define _PERF_SPINLOCK	//	Higher CPU Usage for more responsive packet handling; lower latencies

// Core Classes
namespace PeerNet
{
	enum PacketType : unsigned char
	{
		PN_KeepAlive = 0,
		PN_OrderedACK = 1,
		PN_ReliableACK = 2,
		PN_Ordered = 3,
		PN_Reliable = 4,
		PN_Unreliable = 5
	};
	class SocketRequest;
	class NetPeer;
	class NetSocket;
}

#include "NetAddress.hpp"
#include "NetPacket.h"
#include "NetSocket.h"
#include "NetPeer.h"

namespace PeerNet
{
	//
	//	Basically takes the place of a formal outgoing packet
	class SocketRequest : public OVERLAPPED
	{
	public:
		SocketRequest(NetPacket*const OutgoingPacket, NetPeer*const DestinationPeer) : Packet(OutgoingPacket), Destination(DestinationPeer) {}
		NetPacket*const Packet;
		NetPeer*const Destination;

		const auto GetData() const { return Packet->GetData(); }
		const auto GetDataSize() const { return Packet->GetDataSize(); }
		const auto GetSockAddr() const { return Destination->SockAddr(); }
		const auto GetCreationTime() const { return Packet->GetCreationTime(); }
	};

	//	Initialize PeerNet
	void Initialize();
	//	Deinitialize PeerNet
	void Deinitialize();
}