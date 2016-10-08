#pragma once

namespace PeerNet
{

	class NetPeer
	{
		std::string FormattedAddress;

	public:
		addrinfo* Result;
		NetSocket* const MySocket;
		unsigned long LastReceivedUnreliable = 0;

		unsigned long LatestReceivedReliable = 0;
		unsigned long LatestReceivedReliableACK = 0;
		std::unordered_map<unsigned long, NetPacket*const> q_ReliableAcks;

		unsigned long NextExpectedOrderedID = 1;
		unsigned long NextExpectedOrderedACK = 1;
		std::unordered_map<unsigned long, NetPacket*const> q_OrderedPackets;
		std::unordered_map<unsigned long, NetPacket*const> q_OrderedAcks;

		unsigned long NextUnreliablePacketID = 1;
		unsigned long NextReliablePacketID = 1;
		unsigned long NextOrderedPacketID = 1;

		NetPeer(const std::string IP, const std::string Port, NetSocket*const Socket) : Result(), MySocket(Socket), q_OrderedPackets(), q_OrderedAcks()
		{
			addrinfo Hint;
			ZeroMemory(&Hint, sizeof(Hint));
			Hint.ai_family = AF_INET;
			Hint.ai_socktype = SOCK_DGRAM;
			Hint.ai_protocol = IPPROTO_UDP;
			Hint.ai_flags = AI_PASSIVE;

			// Resolve the servers addrinfo
			if (getaddrinfo(IP.c_str(), Port.c_str(), &Hint, &Result) != 0) {
				printf("GetAddrInfo Failed(%i)\n", WSAGetLastError());
			}

			if (Result->ai_family == AF_INET)
			{
				char*const ResolvedIP = new char[16];
				inet_ntop(AF_INET, &(((sockaddr_in*)((sockaddr*)Result->ai_addr))->sin_addr), ResolvedIP, 16);
				FormattedAddress = ResolvedIP + std::string(":") + Port;
				delete[] ResolvedIP;
			}
			else {
				//return &(((struct sockaddr_in6*)sa)->sin6_addr);
			}

			//	Send out our discovery request
			MySocket->AddOutgoingPacket(CreateNewPacket(PacketType::PN_Discovery));
			printf("Create Peer - %s\n", FormattedAddress.c_str());
		}

		~NetPeer() { freeaddrinfo(Result); }

		//	Construct and return a NetPacket to fill and send to this NetPeer
		//	ToDo: PacketID will clash on socket if same socket is used to send packets for two different peers and each peer chooses the same PacketID
		NetPacket* CreateNewPacket(PacketType pType) {
			if (pType == PacketType::PN_Ordered)
			{
				return new NetPacket(NextOrderedPacketID++, pType, this);
			}
			else if (pType == PacketType::PN_Reliable)
			{
				return new NetPacket(NextReliablePacketID++, pType, this);
			}
			else {
				return new NetPacket(NextUnreliablePacketID++, pType, this);
			}
}

		//	Called from a NetSocket's Receive Thread
		void ReceivePacket(NetPacket* IncomingPacket)
		{
			switch (IncomingPacket->GetType()) {

				//
				//
				//
				//	PN_OrderedACK
				//	Acknowledgements are passed to the NetPeer for further handling
			case PacketType::PN_OrderedACK:
				if (IncomingPacket->GetPacketID() < NextExpectedOrderedACK) { delete IncomingPacket; return; }

				q_OrderedAcks.insert(std::make_pair(IncomingPacket->GetPacketID(), IncomingPacket));
				//	is packet id > expected id? return.
				if (IncomingPacket->GetPacketID() > NextExpectedOrderedACK) { return; }

				//	is packet id == expected id? process packet. increment expected id.
				++NextExpectedOrderedACK;
				//
				//	Process your reliable packet here
				//
#ifdef _DEBUG_PACKETS_ORDERED_ACK
				printf("Recv Ordered Ack 1 - %u\n", IncomingPacket->GetPacketID());
#endif
				while (!q_OrderedAcks.empty())
				{
					auto got = q_OrderedAcks.find(NextExpectedOrderedACK);	//	See if we've already received our next expected packet.
					if (got == q_OrderedAcks.end()) { return; }	//	Not found; break loop.
					++NextExpectedOrderedACK;	//	Found; Increment our counter.
												//
												//	Process your reliable packet here
												//
												//	got->second
												//
#ifdef _DEBUG_PACKETS_ORDERED_ACK
					printf("Recv Ordered Ack 2 - %u\n", got->second->GetPacketID());
#endif
					//	Continue the loop until we run out of matches or our queue winds up empty.
				}
				break;

				//
				//
				//
				//	PN_ReliableACK
			//	Acknowledgements are passed to the NetPeer for further handling
			case PacketType::PN_ReliableACK:
				if (IncomingPacket->GetPacketID() <= LatestReceivedReliableACK) { delete IncomingPacket; break; }
				q_ReliableAcks.insert(std::make_pair(IncomingPacket->GetPacketID(), IncomingPacket));
				LatestReceivedReliableACK = IncomingPacket->GetPacketID();
#ifdef _DEBUG_PACKETS_RELIABLE_ACK
				printf("Reliable Ack - %u\n", IncomingPacket->GetPacketID());
#endif
				break;

				//
				//
				//
				//	PN_Ordered
			//	Ordered packets wait and pass received packets numerically to peers
			case PacketType::PN_Ordered:
				{
					MySocket->AddOutgoingPacket(new NetPacket(IncomingPacket->GetPacketID(), PacketType::PN_OrderedACK, this));

					//	is packet id less than expected id? delete. it's already acked
					if (IncomingPacket->GetPacketID() < NextExpectedOrderedID) { delete IncomingPacket; return; }

					//	is packet id > expected id? store in map, key is packet id.
					if (IncomingPacket->GetPacketID() > NextExpectedOrderedID) { q_OrderedPackets.insert(std::make_pair(IncomingPacket->GetPacketID(), IncomingPacket)); return; }

					//	is packet id == expected id? process packet. delete. increment expected id.
					++NextExpectedOrderedID;
					//
					//	Process your reliable packet here
					//
#ifdef _DEBUG_PACKETS_ORDERED
					printf("Recv Ordered Packet - %u\n", IncomingPacket->GetPacketID());
#endif
					delete IncomingPacket;
					while (!q_OrderedPackets.empty())
					{
						auto got = q_OrderedPackets.find(NextExpectedOrderedID);	//	See if we've already received our next expected packet.
						if (got == q_OrderedPackets.end()) { return; }	//	Not found; break loop.
						++NextExpectedOrderedID;	//	Found; Increment our counter.
						//
						//	Process your reliable packet here
						//
						//	got->second
						//
#ifdef _DEBUG_PACKETS_ORDERED
						printf("Recv Ordered Packet - %u\n", got->second->GetPacketID());
#endif
						//	We're finished with this packet; clean it up.
						delete got->second;
						q_OrderedPackets.erase(got);
						//	Continue the loop until we run out of matches or our queue winds up empty.
					}
				}
				break;

				//
				//
				//
				//	PN_Reliable
			//	Reliable packets always ACK immediatly
			case PacketType::PN_Reliable:
				MySocket->AddOutgoingPacket(new NetPacket(IncomingPacket->GetPacketID(), PacketType::PN_ReliableACK, this));
				//	Only accept the most recent received reliable packets
				if (IncomingPacket->GetPacketID() <= LatestReceivedReliable) { delete IncomingPacket; break; }
				LatestReceivedReliable = IncomingPacket->GetPacketID();
#ifdef _DEBUG_PACKETS_RELIABLE
				printf("Recv Reliable - %u\n", IncomingPacket->GetPacketID());
#endif
				delete IncomingPacket;
				break;

				//
				//
				//
				//	PN_Unreliable
			//	Unreliable packets are given to peers reguardless the condition
			case PacketType::PN_Unreliable:
				//	Only accept the most recent received unreliable packets
				if (IncomingPacket->GetPacketID() <= LastReceivedUnreliable) { delete IncomingPacket; break; }
				LastReceivedUnreliable = IncomingPacket->GetPacketID();
#ifdef _DEBUG_PACKETS_UNRELIABLE
				printf("Recv Unreliable - %u\n", IncomingPacket->GetPacketID());
#endif
				delete IncomingPacket;
				break;

				//
				//
				//
				//	PN_Discovery
			//	Special case packet implementing the discovery protocol
			case PacketType::PN_Discovery:
				//	We're receiving an acknowledgement for a request we created
				MySocket->AddOutgoingPacket(new NetPacket(IncomingPacket->GetPacketID(), PacketType::PN_ReliableACK, this));	//	Send Acknowledgement
				delete IncomingPacket;
				break;

			default:
				printf("Recv Unknown Packet Type\n");
				delete IncomingPacket;
			}

		}

		//	Final step before a reliable packet is sent or resent
		const bool SendPacket_Reliable(NetPacket* Packet)
		{
			//	No ack received yet; check packet if needs delete.
			if (Packet->GetPacketID() > LatestReceivedReliableACK) { return !Packet->NeedsDelete(); }

			//	Check if we have an ack packet tucked away
			auto got = q_ReliableAcks.find(Packet->GetPacketID());	//	See if we've already received our next expected packet.
			if (got == q_ReliableAcks.end()) { return false; }	//	Not found; break loop.
			//Performance Counting here
			printf("\tReliable - %i -\t %.3fms\n", Packet->GetPacketID(), (std::chrono::duration<double, std::milli>(got->second->GetCreationTime() - Packet->GetCreationTime()).count()));
			//	We're finished with the ack packet; clean it up.
			delete got->second;
			q_ReliableAcks.erase(got);
			return false; // we will now be deleted
		}

		//	ToDo: Process an ordered packet someone has sent us

		const std::string GetFormattedAddress() const { return FormattedAddress; }

		//	Send a packet
		//	Do not ever touch the packet again after calling this
		void SendPacket( NetPacket* Packet) { MySocket->AddOutgoingPacket(Packet); }
	};

}