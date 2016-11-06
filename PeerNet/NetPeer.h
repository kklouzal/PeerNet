#pragma once
#include "TimedEvent.hpp"
#include "Channel.hpp"

namespace PeerNet
{
	class NetPeer : public TimedEvent
	{
		const NetAddress*const Address;

		KeepAliveChannel<PacketType::PN_KeepAlive>* CH_KOL;
		OrderedChannel<PacketType::PN_Ordered>* CH_Ordered;
		ReliableChannel<PacketType::PN_Reliable>* CH_Reliable;
		UnreliableChannel<PacketType::PN_Unreliable>* CH_Unreliable;

		void OnTick();
		void OnExpire();

	public:
		NetSocket*const Socket;

		NetPeer(const std::string StrIP, const std::string StrPort, NetSocket*const DefaultSocket);
		~NetPeer();

		//	Construct and return a NetPacket to fill and send to this NetPeer
		auto NetPeer::CreateNewPacket(const PacketType pType) {
			if (pType == PacketType::PN_Ordered)
			{
				return CH_Ordered->NewPacket();
			}
			else if (pType == PacketType::PN_Reliable)
			{
				return CH_Reliable->NewPacket();
			}
			else if (pType == PacketType::PN_KeepAlive)
			{
				return CH_KOL->NewPacket();
			}
			return CH_Unreliable->NewPacket();
		}

		void SendPacket(NetPacket* Packet);
		void ReceivePacket(NetPacket* IncomingPacket);

		const auto RTT_KOL() const { return CH_KOL->RTT(); }

		const auto FormattedAddress() const { return Address->FormattedAddress(); }
		const auto SockAddr() const { return Address->SockAddr(); }
	};
}