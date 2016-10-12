#pragma once

namespace PeerNet
{

	class NetPeer
	{
		const NetAddress*const Address;
		NetSocket*const Socket;

	public:
		unsigned long LastReceivedUnreliable = 0;

		unsigned long LatestReceivedReliable = 0;
		unsigned long LatestReceivedReliableACK = 0;
		std::unordered_map<unsigned long, NetPacket*const> ReliablePkts;
		std::unordered_map<unsigned long, NetPacket*const> ReliableAcks;

		unsigned long NextExpectedOrderedID = 1;
		unsigned long NextExpectedOrderedACK = 1;
		std::unordered_map<unsigned long, NetPacket*const> OrderedPkts;
		std::unordered_map<unsigned long, NetPacket*const> OrderedAcks;

		unsigned long NextUnreliablePacketID = 1;
		unsigned long NextReliablePacketID = 1;
		unsigned long NextOrderedPacketID = 1;

		NetPeer(const std::string StrIP, const std::string StrPort, NetSocket * const DefaultSocket);

		NetPacket * CreateNewPacket(PacketType pType);
		void SendPacket(NetPacket * Packet);
		void ReceivePacket(NetPacket * IncomingPacket);

		const bool SendPacket_Reliable(NetPacket * Packet);

		const std::string FormattedAddress() const { return Address->FormattedAddress(); }
		const sockaddr*const SockAddr() const { return Address->SockAddr(); }

	};

}