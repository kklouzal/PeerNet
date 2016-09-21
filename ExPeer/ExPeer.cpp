#include <PeerNet.h>
#pragma comment(lib, "PeerNet.lib")

#include <iostream>
#include <string>

int main()
{
	std::string ConsoleInput;
	printf("[Example Peer]\nHelp commands->\n");
	printf("\tinit - Initialize PeerNet\n");
	printf("\tdeinit - Deinitialize PeerNet\n");
	printf("\tquit - Exit application\n");
	printf("\topen - Create new socket from IP and PORT\n");
	printf("\tclose - Delete the last created socket\n");
	printf("\tdiscover - Discover new peer from IP and PORT\n");
	printf("\tforget - Forget a discovered peer\n");
	printf("\tr - Send reliable packet to discovered peer\n");
	printf("\tur - Send unreliable packet to discovered peer\n");
	printf("\n");

	PeerNet::NetSocket* Socket = nullptr;
	std::shared_ptr<PeerNet::NetPeer> Peer;
	while (std::getline(std::cin, ConsoleInput))
	{
		if (ConsoleInput == "init")	{
			PeerNet::Initialize();
		}
		else if (ConsoleInput == "deinit") {
			PeerNet::Deinitialize();
		}
		if (ConsoleInput == "quit")	{
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
				Socket = PeerNet::CreateSocket(InputIP.c_str(), InputPort.c_str());
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
				Peer = Socket->DiscoverPeer(InputIP.c_str(), InputPort.c_str());
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
				auto NewPacket = PeerNet::CreateNewPacket(PeerNet::PacketType::PN_Reliable);
				NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
				Peer->SendPacket(NewPacket);
			}
		}
		else if (ConsoleInput == "u")
		{
			if (Peer != nullptr)
			{
				auto NewPacket = PeerNet::CreateNewPacket(PeerNet::PacketType::PN_Unreliable);
				NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable..");
				Peer->SendPacket(NewPacket);
			}
		}
		else if (ConsoleInput == "ul")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 1000)
				{
					auto NewPacket = PeerNet::CreateNewPacket(PeerNet::PacketType::PN_Unreliable);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable..");
					Peer->SendPacket(NewPacket);
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
					auto NewPacket = PeerNet::CreateNewPacket(PeerNet::PacketType::PN_Reliable);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
					Peer->SendPacket(NewPacket);
					i++;
				}
			}
		}
	}
	std::system("PAUSE");
	return 0;
}