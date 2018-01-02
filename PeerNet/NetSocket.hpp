#pragma once

#define PN_MaxPacketSize 1472		//	Max size of an outgoing or incoming packet
#define RIO_ResultsPerThread 128	//	How many results to dequeue from the stack per thread
#define PN_MaxSendPackets 10240		//	Max outgoing packets per socket before you run out of memory
#define PN_MaxReceivePackets 10240	//	Max pending incoming packets before new packets are disgarded

#include "ThreadPoolSend.hpp"
#include "ThreadPoolReceive.hpp"


namespace PeerNet
{
	//
	//	NetSocket Class
	//
	class NetSocket
	{
	public:
		PeerNet* _PeerNet = nullptr;
		NetAddress* Address = nullptr;
		SOCKET Socket;
		//	RIO Function Table
		RIO_EXTENSION_FUNCTION_TABLE RIO;
		//	Request Queue
		std::mutex RioMutex;
		RIO_RQ RequestQueue;

		ThreadPoolReceive* ReceivePool;
		ThreadPoolSend* SendPool;


		//
		//	NetSocket Constructor
		//
		inline NetSocket(PeerNet* PNInstance, NetAddress* MyAddress) : _PeerNet(PNInstance), Address(MyAddress), RIO(_PeerNet->RIO()),
			Socket(WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL, WSA_FLAG_REGISTERED_IO | WSA_FLAG_OVERLAPPED)), RioMutex()
		{
			//	Make sure our socket was created properly
			if (Socket == INVALID_SOCKET) { printf("Socket Failed(%i)\n", WSAGetLastError()); }
			
			ReceivePool = new ThreadPoolReceive(PNInstance, RequestQueue, &RioMutex);
			SendPool = new ThreadPoolSend(PNInstance, RequestQueue, &RioMutex);

			//	Create Request Queue
			RequestQueue = RIO.RIOCreateRequestQueue(Socket, PN_MaxReceivePackets, 1, PN_MaxSendPackets, 1, ReceivePool->GetCompletionQueue(), SendPool->GetCompletionQueue(), NULL);
			if (RequestQueue == RIO_INVALID_RQ) { printf("Request Queue Failed: %i\n", WSAGetLastError()); }

			//	Initialize our ReceivePool
			ReceivePool->Initialize(RequestQueue);
			//	Initialize our SendPool
			SendPool->Initialize(RequestQueue);

			//	Finally bind our socket so we can send/receive data
			if (bind(Socket, Address->AddrInfo()->ai_addr, (int)Address->AddrInfo()->ai_addrlen) == SOCKET_ERROR)
			{ printf("Bind Failed(%i)\n", WSAGetLastError()); }
			else
			{ printf("\tListening On - %s\n", Address->FormattedAddress()); }
		}

		//
		//	NetSocket Destructor
		//
		inline ~NetSocket()
		{
			shutdown(Socket, SD_BOTH);	//	Prohibit Socket from conducting any more Sends or Receives
			delete ReceivePool;			//	Cleanup ReceivePool
			delete SendPool;			//	Cleanup SendPool
			closesocket(Socket);		//	Shutdown Socket

			printf("\tShutdown Socket - %s\n", Address->FormattedAddress());
			//	Cleanup our NetAddress
			//	TODO: Need to return this address back into the Unused Address Pool instead of deleting it
			delete Address;
		}

		inline void SendPacket(SendPacket* Packet)
		{
			SendPool->PostCompletion(CK_SEND, Packet);
		}
	};
}