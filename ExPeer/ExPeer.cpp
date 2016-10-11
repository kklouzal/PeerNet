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
		else if (ConsoleInput == "o")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 32)
				{
					auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Ordered);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm ordered!!");
					Peer->SendPacket(NewPacket);
					i++;
				}
			}
		}
		else if (ConsoleInput == "r")
		{
			if (Peer != nullptr)
			{
				auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Reliable);
				NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
				Peer->SendPacket(NewPacket);
			}
		}
		else if (ConsoleInput == "u")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 10000)
				{
					auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Unreliable);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable!!");
					Peer->SendPacket(NewPacket);
					i++;
				}
			}
		}
		else if (ConsoleInput == "tp")	//	Test Thread Pooling
		{
			//	IOCP Thread Pool with a User Defined Completion Function
			ThreadPoolIOCP* TPool = new ThreadPoolIOCP([](const DWORD numberOfBytes, const ULONG_PTR completionKey, const OVERLAPPED*const pOverlapped) {
				//printf("Bytes %i Key: %i\n", numberOfBytes, completionKey);
			});
			printf("Posting 10,000,000 Completions..");
			for (unsigned int i = 1; i < 10000000; i++)
			{
				TPool->PostCompletion(i);
			}
			printf(".Done!\n");
			delete TPool;
		}
	}
	std::system("PAUSE");
	return 0;
}