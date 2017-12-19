#include "PeerNet.h"

namespace PeerNet
{
	PeerNet * PeerNet::_instance = nullptr;

	PeerNet::PeerNet(unsigned int MaxPeers, unsigned int MaxSockets) {
		printf("Initializing PeerNet\n");
		//	Startup WinSock 2.2
		const size_t iResult = WSAStartup(MAKEWORD(2, 2), &WSADATA());
		if (iResult != 0) {
			printf("\tWSAStartup Error: %i\n", (int)iResult);
		}
		else {
			//	Create a dummy socket long enough to get our RIO Function Table pointer
			SOCKET RioSocket = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL, WSA_FLAG_REGISTERED_IO);
			GUID functionTableID = WSAID_MULTIPLE_RIO;
			DWORD dwBytes = 0;
			if (WSAIoctl(RioSocket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
				&functionTableID,
				sizeof(GUID),
				(void**)&g_rio,
				sizeof(g_rio),
				&dwBytes, 0, 0) == SOCKET_ERROR) {
				printf("RIO Failed(%i)\n", WSAGetLastError());
			}
			closesocket(RioSocket);

			//	Create the Address Pool handlers
			PeerKeeper = new AddressPool<NetPeer*>(g_rio, MaxPeers);
			SocketKeeper = new AddressPool<NetSocket*>(g_rio, MaxSockets);

			//	TODO: Initialize our send/receive packets
			printf("Initialization Complete\n");
		}
	}

	PeerNet::~PeerNet()
	{
		printf("Deinitializing PeerNet\n");

		WSACleanup();
		delete PeerKeeper;
		delete SocketKeeper;
		printf("Deinitialization Complete\n");
	}

	//	Initialize PeerNet
	PeerNet* PeerNet::Initialize(unsigned int MaxPeers, unsigned int MaxSockets)
	{
		if (_instance == nullptr)
		{
			//	Should be initializing from the main thread
			//	Set the applications scheduling priority one tick higher
			SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
			_instance = new PeerNet(MaxPeers, MaxSockets);
		}
		return _instance;
	}

	//	Deinitialize PeerNet
	void PeerNet::Deinitialize()
	{
		if (_instance != nullptr)
		{
			delete _instance;
			_instance = nullptr;
		}
	}

	//	Creates a socket and starts listening at the specified IP and Port
	//	Returns socket if it already exists
	NetSocket*const PeerNet::OpenSocket(string StrIP, string StrPort)
	{
		NetSocket* ExistingSocket = NULL;
		NetAddress* NewAddress;
		//	Can we create a new NetSocket with this ip/port?
		if (SocketKeeper->New(StrIP, StrPort, ExistingSocket, NewAddress))
		{
			NetSocket* ThisSocket = new NetSocket(this, NewAddress);
			SocketKeeper->InsertConnected(NewAddress, ThisSocket);
			return ThisSocket;
		}
		//	No available connections or object already connected
		return ExistingSocket;
	}

	//	Creates and connects to a peer at the specified IP and Port
	//	Returns peer if it already exists
	//
	//	ToDo: The NetAddress created through NewAddress needs to be returned to PeerKeeper's Unused container
	//	when this NetPeer is cleaned up
	NetPeer*const PeerNet::ConnectPeer(string StrIP, string StrPort, NetSocket* DefaultSocket)
	{
		if (DefaultSocket == nullptr) { printf("Error: DefaultSocket NULL\n"); return nullptr; }
		NetPeer* ExistingPeer = nullptr;
		NetAddress* NewAddress = nullptr;
		//	Can we create a new NetPeer with this ip/port?
		if (PeerKeeper->New(StrIP, StrPort, ExistingPeer, NewAddress))
		{
			NetPeer* ThisPeer = new NetPeer(DefaultSocket, NewAddress);
			PeerKeeper->InsertConnected(NewAddress, ThisPeer);
			return ThisPeer;
		}
		//	No available connections or object already connected
		return ExistingPeer;
	}

	//	Need DisconnectPeer/CloseSocket to properly cleanup our internal containers
	//	Or split those functions up into their respective files
	//	And let their respective classes destructors handle it <--
	void PeerNet::DisconnectPeer(SOCKADDR_INET* AddrBuff)
	{

	}

	//	Checks for an existing connected peer and returns it
	//	Or returns a newly constructed NetPeer and immediatly sends the discovery packet
	NetPeer*const PeerNet::GetPeer(SOCKADDR_INET* AddrBuff, NetSocket* DefaultSocket)
	{
		//	See if we already have an existing peer
		NetPeer* ThisPeer = PeerKeeper->GetExisting(AddrBuff);
		if (ThisPeer != nullptr) { return ThisPeer; }

		const string SenderIP(inet_ntoa(AddrBuff->Ipv4.sin_addr));
		const string SenderPort(std::to_string(ntohs(AddrBuff->Ipv4.sin_port)));

		return ConnectPeer(SenderIP, SenderPort, DefaultSocket);
	}
}