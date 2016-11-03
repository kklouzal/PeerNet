#pragma once
#include "TimedEvent.hpp"
#include "OrderedSequence.hpp"

#include "ReliableChannel.hpp"

namespace PeerNet
{
	class NetPeer : public TimedEvent
	{
		const NetAddress*const Address;

		UnreliableChannel<PacketType::PN_Unreliable>* CH_Unreliable;

		ReliableChannel<PacketType::PN_KeepAlive>* CH_KOL;
		ReliableChannel<PacketType::PN_Reliable>* CH_Reliable;


		unsigned long NextExpectedOrdered = 1;
		std::unordered_map<unsigned long, NetPacket*const> IN_OrderedPkts;
		std::mutex IN_OrderedPktMutex;

		std::unordered_map<unsigned long, NetPacket*const> OrderedPkts;
		std::mutex OrderedMutex;
		unsigned long NextExpectedOrderedACK = 1;
		std::unordered_map<unsigned long, NetPacket*const> OrderedAcks;

		unsigned long NextOrderedPacketID = 1;

		void OnTick();
		void OnExpire();

	public:
		NetSocket*const Socket;

		NetPeer(const std::string StrIP, const std::string StrPort, NetSocket*const DefaultSocket);
		~NetPeer();

		//	Construct and return a NetPacket to fill and send to this NetPeer
		auto const NetPeer::CreateNewPacket(const PacketType pType) {
			if (pType == PacketType::PN_Ordered)
			{
				return new NetPacket(NextOrderedPacketID++, pType, this);
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

		void SendPacket(NetPacket*const Packet);
		void ReceivePacket(NetPacket*const IncomingPacket);

		const auto GetAvgKOLRTT() const { return CH_KOL->RTT(); }
		const auto GetAvgOrderedRTT() const { return 0; }
		const auto GetAvgReliableRTT() const { return CH_Reliable->RTT(); }

		const auto FormattedAddress() const { return Address->FormattedAddress(); }
		const auto SockAddr() const { return Address->SockAddr(); }
	};
}