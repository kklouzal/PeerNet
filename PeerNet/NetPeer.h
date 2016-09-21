#pragma once

namespace PeerNet
{

	class NetPeer
	{
		std::mutex IncomingMutex;
		std::deque<NetPacket*> q_IncomingPackets;
	public:
		const std::string StrIP;
		const std::string StrPort;
		addrinfo* Result;

		NetSocket*const MySocket;

		NetPeer(const std::string IP, const std::string Port, NetSocket*const Socket) : StrIP(IP), StrPort(Port), Result(), MySocket(Socket), q_IncomingPackets()
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

		// If we have packets that the user needs processed
		// Return true and swap the contents of the passed deque
		// For the contents of our deque.
		// ToDo: Check if passed deque is empty, if not then empty it?
		/*const bool GetPackets(std::deque<NetPacket*> &Packets)
		{
			if (q_IncomingPackets.empty()) { IncomingMutex.unlock(); return false; }
			IncomingMutex.lock();
			q_IncomingPackets.swap(Packets);
			IncomingMutex.unlock();
			return true;
		}*/

		void SendPacket(NetPacket*const Packet)
		{
			MySocket->AddOutgoingPacket(this, Packet);
		}
	};

}