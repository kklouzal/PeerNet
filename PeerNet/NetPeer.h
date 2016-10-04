#pragma once

namespace PeerNet
{

	class NetPeer
	{
		std::mutex IncomingMutex;
		std::deque<NetPacket*> q_IncomingPackets;
		bool Acknowledged;

		unsigned long LastSuccessfulACK = 0;
		std::chrono::time_point<std::chrono::high_resolution_clock> LastAckTime;

		std::string FormattedAddress;

	public:
		addrinfo* Result;

		NetSocket* const MySocket;

		unsigned long LastReceivedReliable = 0;

		unsigned long NextPacketID = 1;

		const unsigned long GetLastAck() const { return LastSuccessfulACK; }
		const std::chrono::time_point<std::chrono::high_resolution_clock> GetLastAckTime() const { return LastAckTime; }
		void ProcessACK(NetPacket*const Packet)
		{
			LastSuccessfulACK = Packet->GetPacketID();
			LastAckTime = Packet->GetCreationTime();
		}

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

			printf("Discovery Send - %s\n", FormattedAddress.c_str());
		}

		~NetPeer()
		{
			freeaddrinfo(Result);
		}

		void AddPacket(NetPacket*const Packet)
		{
			IncomingMutex.lock();
			q_IncomingPackets.push_back(Packet);
			IncomingMutex.unlock();
		}

		NetPacket* CreateNewPacket(PacketType pType)
		{
			return new NetPacket(NextPacketID++, pType, MySocket, this);
		}

		void SetAcknowledged()
		{
			printf("Discovery Ack - %s\n", FormattedAddress.c_str());
			Acknowledged = true;
		}

		const bool IsAcknowledged() const { return Acknowledged; }
		const std::string GetFormattedAddress() const { return FormattedAddress; }
	};

}