#pragma once

namespace PeerNet
{
	class NetPeer
	{
		const NetAddress*const Address;
		NetSocket*const Socket;

		unsigned long LastReceivedUnreliable = 0;

		const double RollingRTT = 30;	//	Keep a rolling average of the last estimated 15 Round Trip Times
		double AvgReliableRTT = 300;	//	Start the system off assuming a 300ms ping. Let the algorythms adjust from that point.
		double AvgOrderedRTT = 300;		//	**
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
		void AckOrdered(double RTT)
		{
			AvgOrderedRTT -= AvgOrderedRTT / RollingRTT;
			AvgOrderedRTT += RTT / RollingRTT;
		}

		void AckReliable(double RTT)
		{
			AvgReliableRTT -= AvgReliableRTT / RollingRTT;
			AvgReliableRTT += RTT / RollingRTT;
		}

		const double GetAvgRTT() const { return ((AvgReliableRTT+AvgOrderedRTT)/2); }

		NetPeer(const std::string StrIP, const std::string StrPort, NetSocket*const DefaultSocket);

		NetPacket*const CreateNewPacket(const PacketType pType);
		void SendPacket(NetPacket*const Packet);
		void ReceivePacket(NetPacket*const IncomingPacket);


		const auto FormattedAddress() const { return Address->FormattedAddress(); }
		const auto SockAddr() const { return Address->SockAddr(); }

	};

}