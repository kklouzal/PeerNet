#include "PeerNet.h"
#pragma comment(lib, "PeerNet.lib")

#include <iostream>
#include <string>

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
	printf("\to - Send unknown packets to the discovered peer\n");
	printf("\tr - Send reliable packets to the discovered peer\n");
	printf("\tu - Send unreliable packets to the discovered peer\n");
	printf("\n");
	printf("\trtt - Print the discovered peer's RTT's to the console\n");
	
	printf("\n");

	//	Create Socket
	//	Create Peer -> Connect to someone
	//	Add Socket to Peer -> This socket will be used for communication
	//	


	PeerNet::NetSocket* Socket = nullptr;
	PeerNet::NetPeer* Peer = nullptr;

	PeerNet::Initialize();
	while (std::getline(std::cin, ConsoleInput))
	{
		if (ConsoleInput == "quit")	{
			//	1. Delete all your peers
			if (Peer != nullptr)
			{
				delete Peer;
			}
			//	2. Delete all your sockets
			if (Socket != nullptr)
			{
				delete Socket;
			}
			//	3. Shutdown PeerNet
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
				Socket = new PeerNet::NetSocket(InputIP, InputPort);
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
			if (Peer == nullptr)
			{
				printf("IP Address: ");
				std::string InputIP;
				std::getline(std::cin, InputIP);
				printf("Port: ");
				std::string InputPort;
				std::getline(std::cin, InputPort);
				if (InputIP.empty() || InputPort.empty()) { printf("Invalid Arguments\n"); continue; }
				Peer = PeerNet::RetrievePeer(InputIP + std::string(":") + InputPort, Socket);
			}
		}
		else if (ConsoleInput == "forget")
		{
			if (Peer != nullptr)
			{
				delete Peer;
				Peer = nullptr;
			}
		}
		else if (ConsoleInput == "o")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 384)
				{
					auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Ordered);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm ordered!!");
					Peer->SendPacket(NewPacket.get());
					i++;
				}
			}
		}
		else if (ConsoleInput == "r")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 384)
				{
					auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Reliable);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
					Peer->SendPacket(NewPacket.get());
				i++;
				}
			}
		}
		else if (ConsoleInput == "u")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 384)
				{
					auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Unreliable);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable!!");
					Peer->SendPacket(NewPacket.get());
					i++;
				}
			}
		}
		else if (ConsoleInput == "rtt")
		{
			printf("\tKeep-Alive RTT:\t%.3fms\n", Peer->RTT_KOL());
		}
	}
	std::system("PAUSE");
	return 0;
}