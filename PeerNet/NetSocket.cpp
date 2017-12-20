#include "PeerNet.h"

namespace PeerNet
{

	//
	//	NetSocket Constructor
	//
	NetSocket::NetSocket(PeerNet* PNInstance, NetAddress* MyAddress) : _PeerNet(PNInstance), Address(MyAddress),
		Address_Buffer(new char[sizeof(SOCKADDR_INET)*(PN_MaxSendPackets + PN_MaxReceivePackets)]),
		Data_Buffer(new char[PN_MaxPacketSize*(PN_MaxSendPackets + PN_MaxReceivePackets)]),
		Overlapped_Recv(new OVERLAPPED()), Overlapped_Send(new OVERLAPPED()),
		Socket(WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL, WSA_FLAG_REGISTERED_IO)),
		RioMutex_Send(), RioMutex_Receive(), RIO(_PeerNet->RIO()), ThreadPoolIOCP()
	{
		//	Make sure our socket was created properly
		if (Socket == INVALID_SOCKET) { printf("Socket Failed(%i)\n", WSAGetLastError()); }

		//	Create Receive Completion Type and Queue
		RIO_NOTIFICATION_COMPLETION CompletionType_Recv;
		CompletionType_Recv.Type = RIO_IOCP_COMPLETION;
		CompletionType_Recv.Iocp.IocpHandle = this->IOCP();
		CompletionType_Recv.Iocp.CompletionKey = (void*)CK_RIO_RECV;
		CompletionType_Recv.Iocp.Overlapped = Overlapped_Recv;
		CompletionQueue_Recv = RIO.RIOCreateCompletionQueue(PN_MaxReceivePackets, &CompletionType_Recv);
		if (CompletionQueue_Recv == RIO_INVALID_CQ) { printf("Create Completion Queue Failed: %i\n", WSAGetLastError()); }

		//	Create Send Completion Type and Queue
		RIO_NOTIFICATION_COMPLETION CompletionType_Send;
		CompletionType_Send.Type = RIO_IOCP_COMPLETION;
		CompletionType_Send.Iocp.IocpHandle = this->IOCP();
		CompletionType_Send.Iocp.CompletionKey = (void*)CK_RIO_SEND;
		CompletionType_Send.Iocp.Overlapped = Overlapped_Send;
		CompletionQueue_Send = RIO.RIOCreateCompletionQueue(PN_MaxSendPackets + PN_MaxReceivePackets, &CompletionType_Send);
		if (CompletionQueue_Send == RIO_INVALID_CQ) { printf("Create Completion Queue Failed: %i\n", WSAGetLastError()); }

		//	Create Request Queue
		RequestQueue = RIO.RIOCreateRequestQueue(Socket, PN_MaxReceivePackets, 1, PN_MaxSendPackets, 1, CompletionQueue_Recv, CompletionQueue_Send, NULL);
		if (RequestQueue == RIO_INVALID_RQ) { printf("Request Queue Failed: %i\n", WSAGetLastError()); }

		//	Initialize Address Memory Buffer for our receive packets
		Address_BufferID = RIO.RIORegisterBuffer(Address_Buffer, sizeof(SOCKADDR_INET)*PN_MaxReceivePackets);
		if (Address_BufferID == RIO_INVALID_BUFFERID) { printf("Address_Buffer: Invalid BufferID\n"); }

		//	Initialize Data Memory Buffer
		Data_BufferID = RIO.RIORegisterBuffer(Data_Buffer, PN_MaxPacketSize*(PN_MaxSendPackets + PN_MaxReceivePackets));
		if (Data_BufferID == RIO_INVALID_BUFFERID) { printf("Data_Buffer: Invalid BufferID\n"); }

		DWORD ReceiveOffset = 0;

		//	Buffer slices in this loop are for SEND packets
		for (DWORD i = 0; i < PN_MaxSendPackets; i++)
		{
			//
			PRIO_BUF_EXT pBuf = new RIO_BUF_EXT;
			pBuf->BufferId = Data_BufferID;
			pBuf->Offset = ReceiveOffset;
			pBuf->Length = PN_MaxPacketSize;
			pBuf->completionKey = CK_SEND;
			//
			ReceiveOffset += PN_MaxPacketSize;
			//	Figure out which thread we will belong to
			pBuf->MyEnv = GetThreadEnv((unsigned char)floor(i / SendsPerThread));
			pBuf->MyEnv->PushBuffer(pBuf);
		}

		//	Buffer slices in this loop are for RECEIVE packets
		for (DWORD i = 0, AddressOffset = 0; i < PN_MaxReceivePackets; i++)
		{
			//
			PRIO_BUF_EXT pBuf = new RIO_BUF_EXT;
			pBuf->BufferId = Data_BufferID;
			pBuf->Offset = ReceiveOffset;
			pBuf->Length = PN_MaxPacketSize;
			//
			pBuf->pAddrBuff = new RIO_BUF;
			pBuf->pAddrBuff->BufferId = Address_BufferID;
			pBuf->pAddrBuff->Offset = AddressOffset;
			pBuf->pAddrBuff->Length = sizeof(SOCKADDR_INET);
			//	Save our Receive buffer so it can be cleaned up when the socket is destroyed
			Recv_Buffers.push_back(pBuf);
			//
			ReceiveOffset += PN_MaxPacketSize;
			AddressOffset += sizeof(SOCKADDR_INET);
			//
			if (!RIO.RIOReceiveEx(RequestQueue, pBuf, 1, NULL, pBuf->pAddrBuff, NULL, NULL, NULL, pBuf))
			{
				printf("RIO Receive %i Failed %i\n", (int)i, WSAGetLastError());
			}
		}

		//	Finally bind our servers socket so we can listen for data
		if (bind(Socket, Address->AddrInfo()->ai_addr, (int)Address->AddrInfo()->ai_addrlen) == SOCKET_ERROR) { printf("Bind Failed(%i)\n", WSAGetLastError()); }
		//
		if (RIO.RIONotify(CompletionQueue_Send) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }
		if (RIO.RIONotify(CompletionQueue_Recv) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }
		printf("\tListening On - %s\n", Address->FormattedAddress());
	}

	//
	//	NetSocket Destructor
	//
	NetSocket::~NetSocket()
	{
		shutdown(Socket, SD_BOTH);		//	Prohibit Socket from conducting any more Sends or Receives
		this->ShutdownThreads();		//	Shutdown the threads in our Thread Pool
		closesocket(Socket);			//	Shutdown Socket
										//	Cleanup our Completion Queue
		RIO.RIOCloseCompletionQueue(CompletionQueue_Send);
		RIO.RIOCloseCompletionQueue(CompletionQueue_Recv);
		//	Cleanup our RIO Buffers
		RIO.RIODeregisterBuffer(Address_BufferID);
		RIO.RIODeregisterBuffer(Data_BufferID);
		//	Cleanup other memory
		//delete[] uncompressed_data;
		delete Overlapped_Send;
		delete Overlapped_Recv;
		while (!Recv_Buffers.empty())
		{
			PRIO_BUF_EXT Buff = Recv_Buffers.front();
			delete Buff->pAddrBuff;
			delete Buff;
			Recv_Buffers.pop_front();
		}
		delete Address_Buffer;
		delete Data_Buffer;

		printf("\tShutdown Socket - %s\n", Address->FormattedAddress());
		//	Cleanup our NetAddress
		//	TODO: Need to return this address back into the Unused Address Pool instead of deleting it
		delete Address;
	}
}