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
#include <queue>

// Helper Classes
namespace PeerNet
{
	enum PacketType
	{
		PN_ACK = 0,
		PN_Ordered = 1,
		PN_Reliable = 2,
		PN_Unreliable = 3,
		PN_Discovery = 4,
	};
	class NetPacket;
	class NetPeer;
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

	void AddPeer(std::shared_ptr<NetPeer> Peer);
	std::shared_ptr<NetPeer> GetPeer(const std::string FormattedAddress);
}