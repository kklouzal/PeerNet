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

		unsigned long NextExpectedOrderedID = 1;
		std::unordered_map<unsigned long, NetPacket*const> IN_OrderedPkts;
		std::mutex IN_OrderedPktMutex;

		std::unordered_map<unsigned long, NetPacket*const> OrderedPkts;
		std::mutex OrderedPktMutex;
		unsigned long NextExpectedOrderedACK = 1;
		std::unordered_map<unsigned long, NetPacket*const> OrderedAcks;
		std::mutex OrderedAckMutex;

		unsigned long NextUnreliablePacketID = 1;
		unsigned long NextReliablePacketID = 1;
		unsigned long NextOrderedPacketID = 1;
	public:

		NetPeer(const std::string StrIP, const std::string StrPort, NetSocket * const DefaultSocket);

		NetPacket * CreateNewPacket(PacketType pType);
		void SendPacket(NetPacket * Packet);
		void ReceivePacket(NetPacket * IncomingPacket);


		const std::string FormattedAddress() const { return Address->FormattedAddress(); }
		const sockaddr*const SockAddr() const { return Address->SockAddr(); }

	};

}