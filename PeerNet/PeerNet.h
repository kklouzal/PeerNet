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
#include <cereal\types\string.hpp>
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
#define _PERF_SPINLOCK	//	Higher CPU Usage for more responsive packet handling; lower latencies


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
}

#include "NetAddress.hpp"
#include "NetPacket.hpp"

namespace PeerNet
{
	class PeerNet
	{
	private:
		static PeerNet* _instance;

		RIO_EXTENSION_FUNCTION_TABLE g_rio;

		AddressPool* Addresses = nullptr;

		std::unordered_map<string, NetSocket*const> Sockets;
		std::unordered_map<string, NetPeer*const> Peers;

		std::mutex SocketMutex;
		std::mutex PeerMutex;

		NetSocket* DefaultSocket = nullptr;

		//	Private Constructor/Destructor to force Singleton Design Pattern
		PeerNet(unsigned int MaxPeers, unsigned int MaxSockets);
		~PeerNet();

	public:

		//	Initialize PeerNet
		static PeerNet* Initialize(unsigned int MaxPeers, unsigned int MaxSockets);

		//	Deinitialize PeerNet
		static void Deinitialize();

		//	Sets the default socket used by new peers
		void SetDefaultSocket(NetSocket* Socket) { DefaultSocket = Socket; }

		//	Return our Instance
		static inline PeerNet* getInstance() { return _instance; }

		//	Returns access to the RIO Function Table
		inline RIO_EXTENSION_FUNCTION_TABLE& RIO() { return g_rio; }

		//	Creates a socket and starts listening at the specified IP and Port
		//	Returns socket if it already exists
		NetSocket*const OpenSocket(string IP, string Port);

		//	Need DisconnectPeer/CloseSocket to properly cleanup our internal containers
		//	Or split those functions up into their respective files
		//	And let their respective classes destructors handle it <--
		void DisconnectPeer(NetPeer*const Peer);
		void DisconnectPeer(SOCKADDR_INET* AddrBuff);

		//	Transmits a packet over the specified socket
		inline void TransmitPacket(SendPacket*const Packet, NetSocket*const Socket);

		//	Takes raw incoming uncompressed data and an address buffer
		//	Gets a peer from the buffer and passes the data to them for processing
		inline void TranslateData(const SOCKADDR_INET*const AddrBuff, const string& IncomingData);

		//	Gets an existing peer from a provided AddrBuff
		//	Creates a new peer if one does not exist
		inline NetPeer*const PeerNet::GetPeer(const SOCKADDR_INET*const AddrBuff);
		NetPeer*const GetPeer(string IP, string Port);
	};
}

#include "NetSocket.hpp"
#include "NetPeer.hpp"

namespace PeerNet
{
	inline void PeerNet::TransmitPacket(SendPacket*const Packet, NetSocket*const Socket)
	{
		Socket->PostCompletion<SendPacket*const>(CK_SEND, Packet);
	}
	inline void PeerNet::TranslateData(const SOCKADDR_INET*const AddrBuff, const string& IncomingData)
	{
		GetPeer(AddrBuff)->Receive_Packet(IncomingData);
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
			const string IP(inet_ntoa(AddrBuff->Ipv4.sin_addr));
			const string Port(std::to_string(ntohs(AddrBuff->Ipv4.sin_port)));
			NewAddr->Resolve(IP, Port);
			Addresses->WriteAddress(NewAddr);
			NetPeer*const ThisPeer = new NetPeer(this, DefaultSocket, NewAddr);
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
}