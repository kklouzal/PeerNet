#include "PeerNet.h"
#pragma comment(lib, "PeerNet.lib")

#include <iostream>
#include <string>

int main()
{
	std::string ConsoleInput;
	printf("[Example Peer]\nHelp commands->\n");
	printf("\tquit - Exit application\n");
	printf("\topen - Create new socket from IP and PORT\n");
	printf("\tclose - Delete the last created socket\n");
	printf("\tdiscover - Discover new peer from IP and PORT\n");
	printf("\tforget - Forget a discovered peer\n");
	printf("\tr - Send reliable packet to discovered peer\n");
	printf("\trl - Send 1000 reliable packets to discovered peer\n");
	printf("\tu - Send unreliable packet to discovered peer\n");
	printf("\tul - Send 1000 unreliable packet to discovered peer\n");
	printf("\tn - Send unknown packet to discovered peer\n");
	
	printf("\n");

	//	Create Socket
	//	Create Peer -> Connect to someone
	//	Add Socket to Peer -> This socket will be used for communication
	//	


	PeerNet::NetSocket* Socket = nullptr;
	std::shared_ptr<PeerNet::NetPeer> Peer;

	PeerNet::Initialize();
	while (std::getline(std::cin, ConsoleInput))
	{
		if (ConsoleInput == "quit")	{
			PeerNet::Deinitialize();
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
				Socket = PeerNet::CreateSocket(InputIP, InputPort);
			}
		}
		else if (ConsoleInput == "close")
		{
			if (Socket != nullptr)
			{
				PeerNet::DeleteSocket(Socket);
				Socket = nullptr;
			}
		}
		else if (ConsoleInput == "discover")
		{
			if (Peer == nullptr)
			{
				printf("IP Address: ");
				std::string InputIP;
				std::getline(std::cin, InputIP);
				printf("Port: ");
				std::string InputPort;
				std::getline(std::cin, InputPort);
				if (InputIP.empty() || InputPort.empty()) { printf("Invalid Arguments\n"); continue; }
				Peer = Socket->DiscoverPeer(InputIP, InputPort);
			}
		}
		else if (ConsoleInput == "forget")
		{
			if (Peer != nullptr)
			{
				Peer.reset(); // release our shared_ptr to Peer
				Peer = nullptr;
			}
		}
		else if (ConsoleInput == "r")
		{
			if (Peer != nullptr)
			{
				auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Reliable);
				NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
				NewPacket->Send();
			}
		}
		else if (ConsoleInput == "u")
		{
			if (Peer != nullptr)
			{
				auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Unreliable);
				NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable..");
				NewPacket->Send();
			}
		}
		else if (ConsoleInput == "n")
		{
			if (Peer != nullptr)
			{
				auto NewPacket = Peer->CreateNewPacket((PeerNet::PacketType)1001);
				NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable..");
				NewPacket->Send();
			}
		}
		else if (ConsoleInput == "ul")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 1000)
				{
					auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Unreliable);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable..");
					NewPacket->Send();
					i++;
				}
			}
		}
		else if (ConsoleInput == "rl")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 1000)
				{
					auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Reliable);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
					NewPacket->Send();
					i++;
				}
			}
		}
		else if (ConsoleInput == "rl10")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 100000)
				{
					auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Reliable);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
					NewPacket->Send();
					i++;
				}
			}
		}
	}
	std::system("PAUSE");
	return 0;
}