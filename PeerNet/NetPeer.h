#pragma once
#include "TimedEvent.hpp"
#include "Channel.hpp"

namespace PeerNet
{
	class NetPeer : public TimedEvent
	{
		NetAddress*const Address;

		KeepAliveChannel* CH_KOL;
		OrderedChannel* CH_Ordered;
		ReliableChannel* CH_Reliable;
		UnreliableChannel* CH_Unreliable;

		void OnTick();
		void OnExpire();

	public:
		NetSocket*const Socket;

		NetPeer(NetSocket*const DefaultSocket, NetAddress*const NetAddr);

		//NetPeer(const std::string StrIP, const std::string StrPort, NetSocket*const DefaultSocket);
		~NetPeer();

		//	Construct and return a NetPacket to fill and send to this NetPeer
		auto NetPeer::CreateNewPacket(const PacketType pType) {
			if (pType == PN_KeepAlive)
			{
				return CH_KOL->NewPacket();
			}
			else if (pType == PN_Ordered)
			{
				return CH_Ordered->NewPacket();
			}
			else if (pType == PN_Reliable)
			{
				return CH_Reliable->NewPacket();
			}

			return CH_Unreliable->NewPacket();
		}

		void ReceivePacket(NetPacket* IncomingPacket);
		void SendPacket(NetPacket* Packet);

		const auto RTT_KOL() const { return CH_KOL->RTT(); }

		NetAddress*const GetAddress() const { return Address; }
		//const auto FormattedAddress() const { return Address->FormattedAddress(); }
		//const auto SockAddr() const { return Address->SockAddr(); }
	};
}