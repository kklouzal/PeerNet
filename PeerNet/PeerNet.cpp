#include "PeerNet.h"
#pragma comment(lib, "ws2_32.lib")

#include <unordered_map>
#include <deque>
#include <mutex>

using std::string;
using std::to_string;
using std::deque;
using std::unordered_map;
using std::mutex;

namespace PeerNet
{
	namespace
	{
		//	Main Variables
		bool Buffers_Init = false;
		RIO_EXTENSION_FUNCTION_TABLE g_rio;

		AddressPool<NetPeer*, MaxPeers>* PeersKeeper;
		AddressPool<NetSocket*, MaxSockets>* SocketsKeeper;

		NetSocket* Sock_LoopBack = nullptr;
	}

	NetSocket*const OpenSocket(string StrIP, string StrPort)
	{
		printf("Open Socket\n");
		NetSocket* ExistingSocket = NULL;
		NetAddress* NewAddress;
		//	Can we create a new NetSocket with this ip/port?
		if (SocketsKeeper->New(StrIP, StrPort, ExistingSocket, NewAddress))
		{
			printf("Create Socket\n");
			NetSocket* ThisSocket = new NetSocket();
			printf("Set Socket Connected\n");
			SocketsKeeper->InsertConnected(NewAddress, ThisSocket);
			printf("Bind Socket\n");
			ThisSocket->Bind(NewAddress);	//	Final startup procedure for a socket
			printf("Return Socket\n");
			return ThisSocket;
		}
		//	No available connections or object already connected
		printf("Return Socket\n");
		return ExistingSocket;
	}

	NetPeer*const ConnectPeer(string StrIP, string StrPort, NetSocket* DefaultSocket)
	{
		printf("Connect Peer\n");
		if (DefaultSocket == nullptr) { printf("Error: DefaultSocket NULL\n"); return nullptr; }
		NetPeer* ExistingPeer = nullptr;
		NetAddress* NewAddress = nullptr;
		//	Can we create a new NetPeer with this ip/port?
		if (PeersKeeper->New(StrIP, StrPort, ExistingPeer, NewAddress))
		{
			printf("Create Peer\n");
			NetPeer* ThisPeer = new NetPeer(DefaultSocket, NewAddress);
			PeersKeeper->InsertConnected(NewAddress, ThisPeer);
			return ThisPeer;
		}
		//	No available connections or object already connected
		printf("Return Peer\n");
		return ExistingPeer;
	}

	NetPeer*const GetPeer(SOCKADDR_INET* AddrBuff, NetSocket* DefaultSocket)
	{
		//	See if we already have an existing peer
		NetPeer* ThisPeer = PeersKeeper->GetExisting(AddrBuff);
		if (ThisPeer != nullptr) { return ThisPeer; }

		const string SenderIP(inet_ntoa(AddrBuff->Ipv4.sin_addr));
		const string SenderPort(to_string(ntohs(AddrBuff->Ipv4.sin_port)));

		return ConnectPeer(SenderIP, SenderPort, DefaultSocket);
	}

	//	Returns access to the RIO Function Table
	RIO_EXTENSION_FUNCTION_TABLE RIO() { return g_rio; }

	// Public Implementation Methods
	void Initialize()
	{
		//	If Sock_LoopBack is already initialized then PeerNet must be too
		if (Sock_LoopBack != nullptr) { return; }

		//	Startup WinSock 2.2
		const size_t iResult = WSAStartup(MAKEWORD(2, 2), &WSADATA());
		if (iResult != 0) {
			printf("\tWSAStartup Error: %i\n", (int)iResult);
		} else {
			Buffers_Init = false;
			PeersKeeper = new AddressPool<NetPeer*, MaxPeers>();
			SocketsKeeper = new AddressPool<NetSocket*, MaxSockets>();

			//	Create our loopback socket so we can completely initialize PeerNet
			printf("New NetSocket\n");
			Sock_LoopBack = new NetSocket();
			NetAddress* Addr_LoopBack = SocketsKeeper->FreeAddress();
			Addr_LoopBack->Resolve(string("127.0.0.1"), string("9999"));
			Sock_LoopBack->Bind(Addr_LoopBack);
			SocketsKeeper->InsertConnected(Addr_LoopBack, Sock_LoopBack);

			printf("PeerNet Initialized\n");
		}
	}

	void Deinitialize()
	{
		//	Cleanup LoopBack before the rest of the protocol
		delete Sock_LoopBack;
		//
		WSACleanup();
		delete PeersKeeper;
		delete SocketsKeeper;
		printf("PeerNet Deinitialized\n");
	}

	NetSocket * const LoopBack()
	{
		return Sock_LoopBack;
	}

	void InitializeRIO(SOCKET Socket)
	{
		printf("Socket Init Rio\n");
		//	Initialize RIO on this socket
		GUID functionTableID = WSAID_MULTIPLE_RIO;
		DWORD dwBytes = 0;
		if (WSAIoctl(Socket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
			&functionTableID,
			sizeof(GUID),
			(void**)&g_rio,
			sizeof(g_rio),
			&dwBytes, 0, 0) == SOCKET_ERROR) {
			printf("RIO Failed(%i)\n", WSAGetLastError());
		}
		printf("Socket Check Buffers - %d\n", Buffers_Init);
		if (!Buffers_Init)
		{
			printf("Buffers Need Populated\n");
			//	Initialize Peer Address Memory Buffer
			PeersKeeper->Addr_BufferID = g_rio.RIORegisterBuffer(PeersKeeper->Addr_Buffer, sizeof(SOCKADDR_INET)*MaxPeers);
			if (PeersKeeper->Addr_BufferID == RIO_INVALID_BUFFERID) { printf("Peer Address Memory Buffer: Invalid BufferID\n"); }
			for (DWORD i = 0, AddressOffset = 0; i < MaxPeers; i++/*, AddressOffset += sizeof(SOCKADDR_INET)*/)
			{
				NetAddress* PeerAddress = new NetAddress();
				PeerAddress->BufferId = PeersKeeper->Addr_BufferID;
				PeerAddress->Offset = AddressOffset;
				PeerAddress->Length = sizeof(SOCKADDR_INET);
				PeersKeeper->UnusedAddr.push_front(PeerAddress);

				AddressOffset += sizeof(SOCKADDR_INET);
			}
			//	Initialize Socket Address Memory Buffer
			SocketsKeeper->Addr_BufferID = g_rio.RIORegisterBuffer(SocketsKeeper->Addr_Buffer, sizeof(SOCKADDR_INET)*MaxSockets);
			if (SocketsKeeper->Addr_BufferID == RIO_INVALID_BUFFERID) { printf("Socket Address Memory Buffer: Invalid BufferID\n"); }
			for (DWORD i = 0, AddressOffset = 0; i < MaxSockets; i++/*, AddressOffset += sizeof(SOCKADDR_INET)*/)
			{
				NetAddress* SocketAddress = new NetAddress();
				SocketAddress->BufferId = SocketsKeeper->Addr_BufferID;
				SocketAddress->Offset = AddressOffset;
				SocketAddress->Length = sizeof(SOCKADDR_INET);
				SocketsKeeper->UnusedAddr.push_front(SocketAddress);

				AddressOffset += sizeof(SOCKADDR_INET);
			}
			Buffers_Init = true;
		}
		printf("\tBuffer Sizes:\n");
		printf("\tSockets: %I64u\n", SocketsKeeper->UnusedAddr.size());
		printf("\tPeers: %I64u\n", PeersKeeper->UnusedAddr.size());
	}
}