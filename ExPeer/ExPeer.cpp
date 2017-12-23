#include "PeerNet.h"
#ifdef _WIN64
#pragma comment(lib, "PeerNet_x64.lib")
#else
#pragma comment(lib, "PeerNet_Win32.lib")
#endif

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

	PeerNet::PeerNet::Initialize(1024, 16);

	PeerNet::PeerNet *_PeerNet = PeerNet::PeerNet::getInstance();

	PeerNet::NetSocket* Socket = nullptr;
	PeerNet::NetPeer* Peer = nullptr;

	//	New Line before first command entry
	printf("\n");
	while (std::getline(std::cin, ConsoleInput))
	{
		if (ConsoleInput == "quit")	{
			//	1. Delete all your peers
			if (Peer != nullptr)
			{
				//delete Peer;
			}
			//	2. Delete all your sockets
			if (Socket != nullptr)
			{
				//delete Socket;
			}
			//	3. Shutdown PeerNet
			PeerNet::PeerNet::Deinitialize();
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
				delete Peer;
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
				while (i < 1024)
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
			printf("\tKeep-Alive RTT:\t%.3fms\n", Peer->RTT_KOL().count());
		}

		//	New Line before next command entry
		printf("\n");
	}
	printf("Mark Shutdown Memory Here\n");
	system("PAUSE");
	return 0;
}