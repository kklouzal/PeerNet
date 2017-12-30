#include "PeerNet.hpp"

#include <iostream>
#include <string>

//	User-Defined Peer Class - Must inherit from NetPeer
//	Allows the end-user to seamlessly integrate PeerNet into their application
//	by providing their own derived NetPeer(client) class
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
		: PeerNet::NetPeer(PNInstance, DefaultSocket, NetAddr) {
		NewInterval(std::chrono::milliseconds(1000 / 60));	// 60 Ticks every 1 second
	}
};

//	Factory Class for the User-Defined Peer Class
//	Allows the end-user to seamlessly integrate PeerNet into their application
//	by providing their own pre-initialization logic for their derived NetPeer Class
class MyPeerFactory : public PeerNet::NetPeerFactory
{
public:
	inline PeerNet::NetPeer* Create(PeerNet::PeerNet* PNInstance, PeerNet::NetSocket*const DefaultSocket, PeerNet::NetAddress*const NetAddr)
	{
		return new MyPeer(PNInstance, DefaultSocket, NetAddr);
	}
};

//	OperationIDs can be duplicated across different channels
//	However must be unique within the same channel
enum OperationID
{
	Unreliable1 = 0,
	Unreliable2 = 1,
	Unreliable3 = 2,
	Unreliable4 = 3,
	Reliable1 = 0,
	Reliable2 = 1,
	Reliable3 = 2,
	Reliable4 = 3,
	Ordered1 = 0,
	Ordered2 = 1,
	Ordered3 = 2,
	Ordered4 = 3
};

int main()
{
	std::string ConsoleInput;
	printf("[Example Peer]\nHelp commands->\n");
	printf("\n");
	printf("\tDemo:\n");
	printf("\tquit - Exit application\n");
	printf("\n");
	printf("\tSockets:\n");
	printf("\topen - Create new socket from IP and PORT\n");
	printf("\tclose - Delete the last created socket\n");
	printf("\n");
	printf("\tPeers:\n");
	printf("\tdiscover - Discover new peer from IP and PORT\n");
	printf("\tforget - Forget a discovered peer\n");
	printf("\n");
	printf("\tPackets:\n");
	printf("\to0 - Send 16 ordered packets to the discovered peer - Operation 0\n");
	printf("\to1 - Send 256 ordered packets to the discovered peer - Operation 1\n");
	printf("\to2 - Send 1024 ordered packets to the discovered peer - Operation 2\n");
	printf("\to3 - Send 10240 ordered packets to the discovered peer - Operation 3\n");
	printf("\tr0 - Send 16 reliable packets to the discovered peer - Operation 0\n");
	printf("\tr1 - Send 256 reliable packets to the discovered peer - Operation 1\n");
	printf("\tr2 - Send 1024 reliable packets to the discovered peer - Operation 2\n");
	printf("\tr3 - Send 10240 reliable packets to the discovered peer - Operation 3\n");
	printf("\tu0 - Send 16 unreliable packets to the discovered peer - Operation 0\n");
	printf("\tu1 - Send 256 unreliable packets to the discovered peer - Operation 1\n");
	printf("\tu2 - Send 1024 unreliable packets to the discovered peer - Operation 2\n");
	printf("\tu3 - Send 10240 unreliable packets to the discovered peer - Operation 3\n");
	printf("\n");
	printf("\tStatistics:\n");
	printf("\trtt - Print the discovered peer's RTT's to the console\n");
	
	printf("\n");

	//	Create Socket
	//	Create Peer -> Connect to someone
	//	Add Socket to Peer -> This socket will be used for communication
	//	

	printf("Mark Startup Memory Here\n");
	std::system("PAUSE");

	MyPeerFactory* Factory = new MyPeerFactory();
	PeerNet::PeerNet *_PeerNet = new PeerNet::PeerNet(Factory, 10240, 16);

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
		else if (ConsoleInput == "o0")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 16)
				{
					auto NewPacket = Peer->CreateOrderedPacket(OperationID::Ordered1);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm ordered!!");
					Peer->Send_Packet(NewPacket);
					i++;
				}
			}
		}
		else if (ConsoleInput == "o1")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 256)
				{
					auto NewPacket = Peer->CreateOrderedPacket(OperationID::Ordered2);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm ordered!!");
					Peer->Send_Packet(NewPacket);
					i++;
				}
			}
		}
		else if (ConsoleInput == "o2")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 1024)
				{
					auto NewPacket = Peer->CreateOrderedPacket(OperationID::Ordered3);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm ordered!!");
					Peer->Send_Packet(NewPacket);
					i++;
				}
			}
		}
		else if (ConsoleInput == "o3")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 10240)
				{
					auto NewPacket = Peer->CreateOrderedPacket(OperationID::Ordered4);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm ordered!!");
					Peer->Send_Packet(NewPacket);
					i++;
				}
			}
		}
		else if (ConsoleInput == "r0")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 16)
				{
					auto NewPacket = Peer->CreateReliablePacket(OperationID::Reliable1);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
					Peer->Send_Packet(NewPacket);
				i++;
				}
			}
		}
		else if (ConsoleInput == "r1")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 256)
				{
					auto NewPacket = Peer->CreateReliablePacket(OperationID::Reliable2);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
					Peer->Send_Packet(NewPacket);
					i++;
				}
			}
		}
		else if (ConsoleInput == "r2")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 1024)
				{
					auto NewPacket = Peer->CreateReliablePacket(OperationID::Reliable3);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
					Peer->Send_Packet(NewPacket);
					i++;
				}
			}
		}
		else if (ConsoleInput == "r3")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 10240)
				{
					auto NewPacket = Peer->CreateReliablePacket(OperationID::Reliable4);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
					Peer->Send_Packet(NewPacket);
					i++;
				}
			}
		}
		else if (ConsoleInput == "u0")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 16)
				{
					auto NewPacket = Peer->CreateUnreliablePacket(OperationID::Unreliable1);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable!!");
					Peer->Send_Packet(NewPacket);
					i++;
				}
			}
		}
		else if (ConsoleInput == "u1")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 256)
				{
					auto NewPacket = Peer->CreateUnreliablePacket(OperationID::Unreliable2);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable!!");
					Peer->Send_Packet(NewPacket);
					i++;
				}
			}
		}
		else if (ConsoleInput == "u2")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 1024)
				{
					auto NewPacket = Peer->CreateUnreliablePacket(OperationID::Unreliable3);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable!!");
					Peer->Send_Packet(NewPacket);
					i++;
				}
			}
		}
		else if (ConsoleInput == "u3")
		{
			if (Peer != nullptr)
			{
				unsigned int i = 0;
				while (i < 10240)
				{
					auto NewPacket = Peer->CreateUnreliablePacket(OperationID::Unreliable4);
					NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable!!");
					Peer->Send_Packet(NewPacket);
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
		else if (ConsoleInput == "stats")
		{
			if (Peer != nullptr)
			{
				Peer->PrintChannelStats();
			}
		}

		//	New Line before next command entry
		printf("\n");
	}
	printf("Mark Shutdown Memory Here\n");
	system("PAUSE");
	return 0;
}