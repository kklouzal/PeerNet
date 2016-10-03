#pragma once

namespace PeerNet
{

	class NetPeer
	{
		std::mutex IncomingMutex;
		std::deque<NetPacket*> q_IncomingPackets;
		bool Acknowledged;
	public:
		const std::string StrIP;
		const std::string StrPort;
		addrinfo* Result;

		NetSocket*const MySocket;

		unsigned long LastReceivedReliable = 0;
		unsigned long LastSuccessfulACK = 0;

		unsigned long NextPacketID = 1;

		NetPeer(const std::string IP, const std::string Port, NetSocket*const Socket) : StrIP(IP), StrPort(Port), Result(), MySocket(Socket), q_IncomingPackets(), Acknowledged(false)
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
			return new NetPacket(NextPacketID++, pType);
		}

		void SendPacket(NetPacket*const Packet)
		{
			MySocket->AddOutgoingPacket(this, Packet);
		}

		void SetAcknowledged()
		{
			Acknowledged = true;
		}

		const bool IsAcknowledged() const { return Acknowledged; }
	};

}