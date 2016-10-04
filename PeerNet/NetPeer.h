#pragma once

namespace PeerNet
{

	class NetPeer
	{
		std::mutex IncomingMutex;
		std::deque<NetPacket*> q_IncomingPackets;
		std::queue <std::unordered_map<unsigned long, NetPacket*>> q_AcksReceived;
		bool Acknowledged;

		std::chrono::time_point<std::chrono::high_resolution_clock> LastAckTime;

		std::string FormattedAddress;

	public:
		addrinfo* Result;
		NetSocket* const MySocket;
		unsigned long LastReceivedUnreliable = 0;
		unsigned long LastReceivedReliable = 0;
		unsigned long LastReceivedReliableACK = 0;
		unsigned long NextPacketID = 1;

		NetPeer(const std::string IP, const std::string Port, NetSocket*const Socket) : Result(), MySocket(Socket), q_IncomingPackets(), Acknowledged(false)
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

			#ifdef _DEBUG
			printf("Discovery Send - %s\n", FormattedAddress.c_str());
			#else
			printf("Find Peer - %s\n", FormattedAddress.c_str());
			#endif
		}

		~NetPeer() { freeaddrinfo(Result); }

		//	Construct and return a NetPacket to fill and send to this NetPeer
		NetPacket* CreateNewPacket(PacketType pType)
		{
			return new NetPacket(NextPacketID++, pType, MySocket, this);
		}

		//	Process a received Acknowledgement for a packet we've sent out
		void ReceivePacket_ACK(NetPacket* Packet)
		{
			LastReceivedReliableACK = Packet->GetPacketID();
			LastAckTime = Packet->GetCreationTime();
			#ifdef _DEBUG
			printf("Recv Ack #%u\n", Packet->GetPacketID());
			#endif
		}

		//	Process a reliable packet someone has sent us
		void ReceivePacket_Reliable(NetPacket* Packet)
		{
			//	Only accept the most recent received reliable packets
			if (Packet->GetPacketID() <= LastReceivedReliable) { return; }
			LastReceivedReliable = Packet->GetPacketID();
			#ifdef _DEBUG
			printf("Recv Reliable #%u\n", Packet->GetPacketID());
			#endif
		}

		//	Process an unreliable packet someone has sent us
		void ReceivePacket_Unreliable(NetPacket* Packet)
		{
			//	Only accept the most recent received unreliable packets
			if (Packet->GetPacketID() <= LastReceivedUnreliable) { return; }
			LastReceivedUnreliable = Packet->GetPacketID();
			#ifdef _DEBUG
			printf("Recv Unreliable #%u\n", Packet->GetPacketID());
			#endif
		}

		//	ToDo: Process an ordered packet someone has sent us

		//	Called when we have acknowledgement of the discovery process completing
		void SetAcknowledged()
		{
			#ifdef _DEBUG
			printf("Discovery Ack - %s\n", FormattedAddress.c_str());
			#endif
			Acknowledged = true;
		}

		const bool IsAcknowledged() const { return Acknowledged; }
		const unsigned long GetLastReliableAck() const { return LastReceivedReliableACK; }
		const std::string GetFormattedAddress() const { return FormattedAddress; }
		const std::chrono::time_point<std::chrono::high_resolution_clock> GetLastAckTime() const { return LastAckTime; }
	};

}