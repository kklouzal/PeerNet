#pragma once

namespace PeerNet
{
	class NetPeer
	{
		const NetAddress*const Address;
		NetSocket*const Socket;

		unsigned long LastReceivedUnreliable = 0;

		unsigned long LatestReceivedReliable = 0;
		std::unordered_map<unsigned long, NetPacket*const> ReliablePkts;
		std::mutex ReliablePktMutex;

		unsigned long NextExpectedOrdered = 1;
		std::unordered_map<unsigned long, NetPacket*const> IN_OrderedPkts;
		std::mutex IN_OrderedPktMutex;

		std::unordered_map<unsigned long, NetPacket*const> OrderedPkts;
		std::mutex OrderedMutex;
		unsigned long NextExpectedOrderedACK = 1;
		std::unordered_map<unsigned long, NetPacket*const> OrderedAcks;

		unsigned long NextUnreliablePacketID = 1;
		unsigned long NextReliablePacketID = 1;
		unsigned long NextOrderedPacketID = 1;
	public:

		NetPeer(const std::string StrIP, const std::string StrPort, NetSocket*const DefaultSocket);

		NetPacket*const CreateNewPacket(const PacketType pType);
		void SendPacket(NetPacket*const Packet);
		void ReceivePacket(NetPacket*const IncomingPacket);


		const auto FormattedAddress() const { return Address->FormattedAddress(); }
		const auto SockAddr() const { return Address->SockAddr(); }

	};

}