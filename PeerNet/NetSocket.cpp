#include "PeerNet.h"
#include <string>

namespace PeerNet
{
	void NetSocket::OnCompletion(ThreadEnvironment*const Env, const DWORD numberOfBytes, const ULONG_PTR completionKey, OVERLAPPED* pOverlapped)
	{
		//
		//	Thread Completion Function
		//	Process Sends and Receives
		//	(Executed via the Thread Pool)
		//

		switch (completionKey)
		{
		case CK_RIO_RECV:
		{
			const ULONG NumResults = RIO().RIODequeueCompletion(CompletionQueue_Recv, Env->CompletionResults, RIO_ResultsPerThread);
			if (RIO().RIONotify(CompletionQueue_Recv) != ERROR_SUCCESS) { Log("\tRIO Notify Failed\n"); return; }
			if (RIO_CORRUPT_CQ == NumResults) { Log("RIO Corrupt Results\n"); return; }

			//	Actually read the data from each received packet
			for (ULONG CurResult = 0; CurResult < NumResults; CurResult++)
			{
				//	Get the raw packet data into our buffer
				PRIO_BUF_EXT pBuffer = reinterpret_cast<PRIO_BUF_EXT>(Env->CompletionResults[CurResult].RequestContext);

				//	Determine which peer this packet belongs to and pass the data payload off to our NetPeer so they can decompress it according to the TypeID
				GetPeer((SOCKADDR_INET*)&Address_Buffer[pBuffer->pAddrBuff->Offset], this)->Receive_Packet(
					ntohs((u_short)&Data_Buffer[pBuffer->Offset]),
					&Data_Buffer[pBuffer->Offset + sizeof(u_short)],
					Env->CompletionResults[CurResult].BytesTransferred - sizeof(u_short),
					PN_MaxPacketSize - sizeof(u_short),
					Env->Uncompressed_Data,
					Env->Decompression_Context);
#ifdef _PERF_SPINLOCK
				while (!RioMutex.try_lock()) {}
#else
				RioMutex_Receive.lock();
#endif
				//	Push another read request into the queue
				if (!RIO().RIOReceiveEx(RequestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, 0, pBuffer)) { Log("RIO Receive2 Failed\n"); }
				RioMutex_Receive.unlock();
			}
		}
		break;

		case CK_RIO_SEND:
		{
			const ULONG NumResults = RIO().RIODequeueCompletion(CompletionQueue_Send, Env->CompletionResults, RIO_ResultsPerThread);
			if (RIO().RIONotify(CompletionQueue_Send) != ERROR_SUCCESS) { Log("\tRIO Notify Failed\n"); return; }
			if (RIO_CORRUPT_CQ == NumResults) { Log("RIO Corrupt Results\n"); return; }
			//	Actually read the data from each received packet
			for (ULONG CurResult = 0; CurResult < NumResults; CurResult++)
			{
				//	Get the raw packet data into our buffer
				PRIO_BUF_EXT pBuffer = reinterpret_cast<PRIO_BUF_EXT>(Env->CompletionResults[CurResult].RequestContext);

				//	Completion of a send request with the delete flag set
				if (pBuffer->completionKey == CK_SEND_DELETE)
				{
					//	cleanup the NetPacket that initiated this CK_SEND
					delete pBuffer->MyNetPacket;
					//	Reset the completion key
					pBuffer->completionKey = CK_SEND;
					//	Send this pBuffer back to the correct Thread Environment
					pBuffer->MyEnv->PushBuffer(pBuffer);
				} 
				else { pBuffer->MyEnv->PushBuffer(pBuffer); }
			}
		}
		break;

		case CK_SEND:
		{
			SendPacket* OutPacket = reinterpret_cast<SendPacket*>(pOverlapped);
			PRIO_BUF_EXT pBuffer = Env->PopBuffer();
			//	If we are out of buffers push the request back out for another thread to pick up
			if (pBuffer == nullptr) { PostCompletion<SendPacket*>(CK_SEND, OutPacket); return; }
			//	This will allow the CK_SEND RIO completion to cleanup SendPacket when IsManaged() == true
			if (OutPacket->GetManaged())
			{
				pBuffer->MyNetPacket = OutPacket;
				pBuffer->completionKey = CK_SEND_DELETE;
			}

			//	Copy our TypeID into the beginning of the data buffer
			u_short TypeID = htons(OutPacket->GetType());
			std::memcpy(&Data_Buffer[pBuffer->Offset], &TypeID, sizeof(u_short));

			//	Compress our outgoing packets data payload into the rest of the data buffer
			pBuffer->Length = (ULONG)OutPacket->GetPeer()->CompressPacket(OutPacket, &Data_Buffer[pBuffer->Offset + sizeof(u_short)],
				PN_MaxPacketSize-sizeof(u_short), Env->Compression_Context)+sizeof(u_short);

			//	If compression was successful, actually transmit our packet
			if (pBuffer->Length > 0) {
				//printf("Compressed: %i->%i\n", SendPacket->GetData().size(), pBuffer->Length);
#ifdef _PERF_SPINLOCK
				while (!RioMutex.try_lock()) {}
#else
				RioMutex_Send.lock();
#endif
				RIO().RIOSendEx(RequestQueue, pBuffer, 1, NULL, OutPacket->GetPeer()->GetAddress(), NULL, NULL, NULL, pBuffer);
				RioMutex_Send.unlock();
			}
			else { Log("Packet Compression Failed - " + std::to_string(pBuffer->Length) + "\n"); }
		}
		break;
		}
	}

	NetSocket::NetSocket(NetAddress* MyAddress) : Address(MyAddress),
		Address_Buffer(new char[sizeof(SOCKADDR_INET)*(PN_MaxSendPackets + PN_MaxReceivePackets)]),
		Data_Buffer(new char[PN_MaxPacketSize*(PN_MaxSendPackets + PN_MaxReceivePackets)]),
		Overlapped_Recv(new OVERLAPPED()), Overlapped_Send(new OVERLAPPED()),
		Socket(WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL, WSA_FLAG_REGISTERED_IO)),
		RioMutex_Send(), RioMutex_Receive(), ThreadPoolIOCP()
	{
		//	Make sure our socket was created properly
		if (Socket == INVALID_SOCKET) { Log("Socket Failed(" + std::to_string(WSAGetLastError()) + ")\n"); }

		//	Create Receive Completion Type and Queue
		RIO_NOTIFICATION_COMPLETION CompletionType_Recv;
		CompletionType_Recv.Type = RIO_IOCP_COMPLETION;
		CompletionType_Recv.Iocp.IocpHandle = this->IOCP();
		CompletionType_Recv.Iocp.CompletionKey = (void*)CK_RIO_RECV;
		CompletionType_Recv.Iocp.Overlapped = Overlapped_Recv;
		CompletionQueue_Recv = RIO().RIOCreateCompletionQueue(PN_MaxReceivePackets, &CompletionType_Recv);
		if (CompletionQueue_Recv == RIO_INVALID_CQ) { Log("Create Completion Queue Failed: " + std::to_string(WSAGetLastError()) + "\n"); }

		//	Create Send Completion Type and Queue
		RIO_NOTIFICATION_COMPLETION CompletionType_Send;
		CompletionType_Send.Type = RIO_IOCP_COMPLETION;
		CompletionType_Send.Iocp.IocpHandle = this->IOCP();
		CompletionType_Send.Iocp.CompletionKey = (void*)CK_RIO_SEND;
		CompletionType_Send.Iocp.Overlapped = Overlapped_Send;
		CompletionQueue_Send = RIO().RIOCreateCompletionQueue(PN_MaxSendPackets + PN_MaxReceivePackets, &CompletionType_Send);
		if (CompletionQueue_Send == RIO_INVALID_CQ) { Log("Create Completion Queue Failed: " + std::to_string(WSAGetLastError()) + "\n"); }

		//	Create Request Queue
		RequestQueue = RIO().RIOCreateRequestQueue(Socket, PN_MaxReceivePackets, 1, PN_MaxSendPackets, 1, CompletionQueue_Recv, CompletionQueue_Send, NULL);
		if (RequestQueue == RIO_INVALID_RQ) { Log("Request Queue Failed: " + std::to_string(WSAGetLastError()) + "\n"); }

		//	Initialize Address Memory Buffer for our receive packets
		Address_BufferID = RIO().RIORegisterBuffer(Address_Buffer, sizeof(SOCKADDR_INET)*PN_MaxReceivePackets);
		if (Address_BufferID == RIO_INVALID_BUFFERID) { Log("Address_Buffer: Invalid BufferID\n"); }

		//	Initialize Data Memory Buffer
		Data_BufferID = RIO().RIORegisterBuffer(Data_Buffer, PN_MaxPacketSize*(PN_MaxSendPackets+PN_MaxReceivePackets));
		if (Data_BufferID == RIO_INVALID_BUFFERID) { Log("Data_Buffer: Invalid BufferID\n"); }

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
			pBuf->MyEnv = GetThreadEnv((unsigned char)floor(i/SendsPerThread));
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
			if (!RIO().RIOReceiveEx(RequestQueue, pBuf, 1, NULL, pBuf->pAddrBuff, NULL, NULL, NULL, pBuf))
			{	Log("RIO Receive " + std::to_string(i) + " Failed " + std::to_string(WSAGetLastError()) + "\n"); }
		}

		//	Finally bind our servers socket so we can listen for data
		if (bind(Socket, Address->AddrInfo()->ai_addr, (int)Address->AddrInfo()->ai_addrlen) == SOCKET_ERROR) { Log("Bind Failed(" + std::to_string(WSAGetLastError()) + ")\n"); }
		//
		if (RIO().RIONotify(CompletionQueue_Send) != ERROR_SUCCESS) { Log("\tRIO Notify Failed\n"); return; }
		if (RIO().RIONotify(CompletionQueue_Recv) != ERROR_SUCCESS) { Log("\tRIO Notify Failed\n"); return; }
		Log("\tListening On - " + std::string(Address->FormattedAddress()) + "\n");
	}

	//	NetSocket Destructor
	//
	NetSocket::~NetSocket()
	{
		shutdown(Socket, SD_BOTH);		//	Prohibit Socket from conducting any more Sends or Receives
		this->ShutdownThreads();		//	Shutdown the threads in our Thread Pool
		closesocket(Socket);			//	Shutdown Socket
										//	Cleanup our Completion Queue
		RIO().RIOCloseCompletionQueue(CompletionQueue_Send);
		RIO().RIOCloseCompletionQueue(CompletionQueue_Recv);
		//	Cleanup our RIO Buffers
		RIO().RIODeregisterBuffer(Address_BufferID);
		RIO().RIODeregisterBuffer(Data_BufferID);
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
		
		Log("\tShutdown Socket - " + std::string(Address->FormattedAddress()) + "\n");
		//	Cleanup our NetAddress
		delete Address;
	}
}