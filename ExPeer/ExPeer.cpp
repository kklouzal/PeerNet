#include "PeerNet.hpp"

#include <iostream>
#include <string>

class MyPeer : public PeerNet::NetPeer
{
	inline void Receive(PeerNet::ReceivePacket* Packet)
	{
		//printf("Received Packet ID: %i!\n", Packet->GetPacketID());
	}
	inline void Tick()
	{
		//printf("Tick!\n");
	}
public:
	inline MyPeer(PeerNet::PeerNet* PNInstance, PeerNet::NetSocket*const DefaultSocket, PeerNet::NetAddress*const NetAddr)
		: PeerNet::NetPeer(PNInstance, DefaultSocket, NetAddr) {}
};

class MyPeerFactory : public PeerNet::NetPeerFactory
{
public:
	inline PeerNet::NetPeer* Create(PeerNet::PeerNet* PNInstance, PeerNet::NetSocket*const DefaultSocket, PeerNet::NetAddress*const NetAddr)
	{
		return new MyPeer(PNInstance, DefaultSocket, NetAddr);
	}
};

int main()
{
	std::string ConsoleInput;
	printf("[Example Peer]\nHelp commands->\n");
	printf("\n");
	printf("\tquit - Exit application\n");
	printf("\n");
	printf("\topen - Create new socket from IP and PORT\n");
	printf("\tclose - Delete the last created socket\n");
	printf("\n");
	printf("\tdiscover - Discover new peer from IP and PORT\n");
	printf("\tforget - Forget a discovered peer\n");
	printf("\n");
	printf("\to - Send ordered packets to the discovered peer\n");
	printf("\tr - Send reliable packets to the discovered peer\n");
	printf("\tu - Send unreliable packets to the discovered peer\n");
	printf("\n");
	printf("\trtt - Print the discovered peer's RTT's to the console\n");
	
	printf("\n");

	//	Create Socket
	//	Create Peer -> Connect to someone
	//	Add Socket to Peer -> This socket will be used for communication
	//	

	printf("Mark Startup Memory Here\n");
	std::system("PAUSE");

	MyPeerFactory* Factory = new MyPeerFactory();
	PeerNet::PeerNet *_PeerNet = new PeerNet::PeerNet(Factory, 1024, 16);

	PeerNet::NetSocket* Socket = nullptr;
	PeerNet::NetPeer* Peer = nullptr;

	//	New Line before first command entry
	printf("\n");
	while (std::getline(std::cin, ConsoleInput))
	{
		if (ConsoleInput == "quit")	{
			//	Shutdown PeerNet
			delete _PeerNet;
			delete Factory;
			break;
		}
		else if (ConsoleInput == "open") {
			if (Socket == nullptr)
			{
				printf("IP Address: ");
				std::string InputIP;
				std::getline(std::cin, InputIP);
				printf("Port: ");
				std::string InputPort;
				std::getline(std::cin, InputPort);
				if (InputIP.empty() || InputPort.empty()) { printf("Invalid Arguments\n"); continue; }
				Socket = _PeerNet->OpenSocket(InputIP, InputPort);
				_PeerNet->SetDefaultSocket(Socket);
			}
		}
		else if (ConsoleInput == "close")
		{
			if (Socket != nullptr)
			{
				delete Socket;
				Socket = nullptr;
			}
		}
		else if (ConsoleInput == "discover")
		{
			if ((Peer == nullptr) /*&& Socket != nullptr*/)
			{
				printf("IP Address: ");
				std::string InputIP;
				std::getline(std::cin, InputIP);
				printf("Port: ");
				std::string InputPort;
				std::getline(std::cin, InputPort);
				if (InputIP.empty() || InputPort.empty()) { printf("Invalid Arguments\n"); continue; }
				
				Peer = _PeerNet->GetPeer(InputIP, InputPort/*, Socket*/);
			}
		}
		else if (ConsoleInput == "forget")
		{
			if (Peer != nullptr)
			{
				_PeerNet->DisconnectPeer(Peer);
				Peer = nullptr;
			}
		}
		else if (ConsoleInput == "o")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 16)
				{
					auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Ordered);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm ordered!!");
					Peer->Send_Packet(NewPacket.get());
					i++;
				}
			}
		}
		else if (ConsoleInput == "r")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 16)
				{
					auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Reliable);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
					Peer->Send_Packet(NewPacket.get());
				i++;
				}
			}
		}
		else if (ConsoleInput == "u")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 16)
				{
					auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Unreliable);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable!!");
					Peer->Send_Packet(NewPacket.get());
					i++;
				}
			}
		}
		else if (ConsoleInput == "rtt")
		{
			if (Peer != nullptr)
			{
				printf("\tKeep-Alive RTT:\t%.3fms\n", Peer->RTT_KOL().count());
			}
		}

		//	New Line before next command entry
		printf("\n");
	}
	printf("Mark Shutdown Memory Here\n");
	system("PAUSE");
	return 0;
}