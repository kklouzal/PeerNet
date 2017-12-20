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

			//	Create the Address Pool
			Addresses = new AddressPool(g_rio, MaxPeers+MaxSockets);

			//SetDefaultSocket(OpenSocket("127.0.0.1", "9999"));
			//	TODO: Initialize our send/receive packets
			printf("Initialization Complete\n");
		}
	}

	PeerNet::~PeerNet()
	{
		printf("Deinitializing PeerNet\n");
		for (auto Peer : Peers) {
			delete Peer.second;
		}
		for (auto Socket : Sockets) {
			delete Socket.second;
		}
		WSACleanup();
		delete Addresses;
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

	//	Creates and connects to a peer at the specified IP and Port
	//	Returns peer if it already exists
	//
	//	ToDo: The NetAddress created through NewAddress needs to be returned to PeerKeeper's Unused container
	//	when this NetPeer is cleaned up

	//	Need DisconnectPeer/CloseSocket to properly cleanup our internal containers
	//	Or split those functions up into their respective files
	//	And let their respective classes destructors handle it <--
	void PeerNet::DisconnectPeer(SOCKADDR_INET* AddrBuff)
	{

	}

	//	Creates a socket and starts listening at the specified IP and Port
	//	Returns socket if it already exists
	NetSocket*const PeerNet::OpenSocket(string IP, string Port)
	{
		//	Check if we already have a connected object with this address
		const string Formatted(IP + string(":") + Port);
		//SocketMutex.lock();
		auto it = Sockets.find(Formatted);
		if (it != Sockets.end())
		{
			//SocketMutex.unlock();
			return it->second;	//	Already have a connected object for this ip/port
		}
		else {
			NetAddress*const NewAddr = Addresses->FreeAddress();
			NewAddr->Resolve(IP, Port);
			NetSocket*const ThisSocket = new NetSocket(this, NewAddr);
			SocketMutex.lock();	//	Only need to lock here?
			Sockets.emplace(Formatted, ThisSocket);
			SocketMutex.unlock();
			return ThisSocket;
		}
	}


	NetPeer*const PeerNet::GetPeer(string IP, string Port)
	{
		//	Check if we already have a connected object with this address
		const string Formatted(IP + string(":") + Port);
		//PeerMutex.lock();
		auto it = Peers.find(Formatted);
		if (it != Peers.end())
		{
			//PeerMutex.unlock();
			return it->second;	//	Already have a connected object for this ip/port
		}
		else {
			NetAddress*const NewAddr = Addresses->FreeAddress();
			NewAddr->Resolve(IP, Port);
			Addresses->WriteAddress(NewAddr);
			NetPeer*const ThisPeer = new NetPeer(DefaultSocket, NewAddr);
			PeerMutex.lock();	//	Only need to lock here?
			Peers.emplace(Formatted, ThisPeer);
			PeerMutex.unlock();
			return ThisPeer;
		}
	}
}