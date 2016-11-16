#include "PeerNet.h"
#include "lz4.h"

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
		case CK_RIO:
		{
			const ULONG NumResults = RIO().RIODequeueCompletion(CompletionQueue, Env->CompletionResults, RIO_ResultsPerThread);
			if (RIO().RIONotify(CompletionQueue) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }
			if (RIO_CORRUPT_CQ == NumResults) { printf("RIO Corrupt Results\n"); return; }
			//	Actually read the data from each received packet
			for (ULONG CurResult = 0; CurResult < NumResults; CurResult++)
			{
				//	Get the raw packet data into our buffer
				PRIO_BUF_EXT pBuffer = reinterpret_cast<PRIO_BUF_EXT>(Env->CompletionResults[CurResult].RequestContext);

				switch (pBuffer->completionKey)
				{
				case CK_SEND:
				{
					//	Completion of a CK_SEND request

					//	The thread processing this completion may be different from the thread that executed the CK_SEND
					//	Send this pBuffer back to the correct Thread Environment
					pBuffer->MyEnv->PushBuffer(pBuffer);
				}
				break;

				case CK_SEND_DELETE:
				{
					//	Completion of a CK_SEND request with the delete flag set

					//	cleanup the NetPacket that initiated this CK_SEND
					delete pBuffer->MyNetPacket;

					//	Reset the completion key
					pBuffer->completionKey = CK_SEND;

					//	The thread processing this completion may be different from the thread that executed the CK_SEND
					//	Send this pBuffer back to the correct Thread Environment
					pBuffer->MyEnv->PushBuffer(pBuffer);
				}
				break;

				case CK_RECEIVE:
				{
					//	Determine which peer this packet belongs to and pass the data payload off to our NetPeer so they can decompress it according to the TypeID
					GetPeer((SOCKADDR_INET*)&Address_Buffer[pBuffer->pAddrBuff->Offset], this)->Receive_Packet(ntohs((u_short)&Data_Buffer[pBuffer->Offset]), &Data_Buffer[pBuffer->Offset+sizeof(u_short)], Env->CompletionResults[CurResult].BytesTransferred-sizeof(u_short), PN_MaxPacketSize-sizeof(u_short), Env->Uncompressed_Data);
#ifdef _PERF_SPINLOCK
					while (!RioMutex.try_lock()) {}
#else
					RioMutex_Receive.lock();
#endif
					//	Push another read request into the queue
					if (!RIO().RIOReceiveEx(RequestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, 0, pBuffer)) { printf("RIO Receive2 Failed\n"); }
					RioMutex_Receive.unlock();
				}
				break;

				default:
					printf("Unhandled RIO Key: %i\n", (int)pBuffer->completionKey);
				}
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
			pBuffer->Length = OutPacket->GetPeer()->CompressPacket(OutPacket, &Data_Buffer[pBuffer->Offset + sizeof(u_short)], PN_MaxPacketSize-sizeof(u_short))+sizeof(u_short);

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
			else { printf("Packet Compression Failed - %i\n", pBuffer->Length); }
		}
		break;
		}

		//
		//	End Thread Completion Function
		//
	}

	NetSocket::NetSocket(NetAddress* MyAddress) : Address(MyAddress),
		Address_Buffer(new char[sizeof(SOCKADDR_INET)*(PN_MaxSendPackets + PN_MaxReceivePackets)]),
		Data_Buffer(new char[PN_MaxPacketSize*(PN_MaxSendPackets + PN_MaxReceivePackets)]), Overlapped(new OVERLAPPED()),
		Socket(WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL, WSA_FLAG_REGISTERED_IO)),
		RioMutex_Send(), RioMutex_Receive(), ThreadPoolIOCP()
	{
		printf("Initialize Socket\n");
		//	Make sure our socket was created properly
		if (Socket == INVALID_SOCKET) { printf("Socket Failed(%i)\n", WSAGetLastError()); }

		//	Create Completion Queue
		RIO_NOTIFICATION_COMPLETION CompletionType;
		CompletionType.Type = RIO_IOCP_COMPLETION;
		CompletionType.Iocp.IocpHandle = this->IOCP();
		CompletionType.Iocp.CompletionKey = (void*)CK_RIO;
		CompletionType.Iocp.Overlapped = Overlapped;
		CompletionQueue = RIO().RIOCreateCompletionQueue(PN_MaxSendPackets + PN_MaxReceivePackets, &CompletionType);
		if (CompletionQueue == RIO_INVALID_CQ) { printf("Create Completion Queue Failed: %i\n", WSAGetLastError()); }

		//	Create Request Queue
		RequestQueue = RIO().RIOCreateRequestQueue(Socket, PN_MaxReceivePackets, 1, PN_MaxSendPackets, 1, CompletionQueue, CompletionQueue, NULL);
		if (RequestQueue == RIO_INVALID_RQ) { printf("Request Queue Failed: %i\n", WSAGetLastError()); }

		//	Initialize Address Memory Buffer
		Address_BufferID = RIO().RIORegisterBuffer(Address_Buffer, sizeof(SOCKADDR_INET)*(PN_MaxSendPackets + PN_MaxReceivePackets));
		if (Address_BufferID == RIO_INVALID_BUFFERID) { printf("Address_Buffer: Invalid BufferID\n"); }

		//	Initialize Data Memory Buffer
		Data_BufferID = RIO().RIORegisterBuffer(Data_Buffer, PN_MaxPacketSize*(PN_MaxSendPackets+PN_MaxReceivePackets));
		if (Data_BufferID == RIO_INVALID_BUFFERID) { printf("Data_Buffer: Invalid BufferID\n"); }

		//	Split Data Buffer into chunks and construct a new RIO_BUF_EXT for each slice
		//	Take a corresponding slice from our Address Buffer and construct a regular RIO_BUF
		//	Queue up the amount of receives specified in our constructor
		//	The left overs will be reserved for sends
		for (DWORD i = 0, ReceiveOffset = 0, AddressOffset = 0; i < (PN_MaxSendPackets+PN_MaxReceivePackets); i++)
		{
			PRIO_BUF_EXT pBuf = new RIO_BUF_EXT;
			pBuf->BufferId = Data_BufferID;
			pBuf->Offset = ReceiveOffset;
			pBuf->Length = PN_MaxPacketSize;
			pBuf->pAddrBuff = new RIO_BUF;
			pBuf->pAddrBuff->BufferId = Address_BufferID;
			pBuf->pAddrBuff->Offset = AddressOffset;
			pBuf->pAddrBuff->Length = sizeof(SOCKADDR_INET);

			pBuf->ThreadNumber = (unsigned char)floor(i / SendsPerThread);

			ReceiveOffset += PN_MaxPacketSize;
			AddressOffset += sizeof(SOCKADDR_INET);

			if (i < PN_MaxSendPackets)
			{
				//	This buffer will be used for sends so add it to our queue
				pBuf->completionKey = CK_SEND;
				//	Figure out which thread we will belong to
				pBuf->MyEnv = GetThreadEnv(pBuf->ThreadNumber);
				pBuf->MyEnv->PushBuffer(pBuf);
			} else {
				pBuf->completionKey = CK_RECEIVE;
				if (!RIO().RIOReceiveEx(RequestQueue, pBuf, 1, NULL, pBuf->pAddrBuff, NULL, NULL, NULL, pBuf))
				{
					printf("RIO Receive %i Failed %i\n", (int)i, WSAGetLastError());
				}
			}
		}

		//	Finally bind our servers socket so we can listen for data
		if (bind(Socket, Address->AddrInfo()->ai_addr, (int)Address->AddrInfo()->ai_addrlen) == SOCKET_ERROR) { printf("Bind Failed(%i)\n", WSAGetLastError()); }
		//
		if (RIO().RIONotify(CompletionQueue) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }
		printf("\tListening On - %s\n", Address->FormattedAddress());
	}

	//	NetSocket Destructor
	//
	NetSocket::~NetSocket()
	{
		printf("\tClose Socket - %s\n", Address->FormattedAddress());
		shutdown(Socket, SD_BOTH);		//	Prohibit Socket from conducting any more Sends or Receives
		this->ShutdownThreads();		//	Shutdown the threads in our Thread Pool
		closesocket(Socket);			//	Shutdown Socket
										//	Cleanup our Completion Queue
		RIO().RIOCloseCompletionQueue(CompletionQueue);
		//	Cleanup our RIO Buffers
		RIO().RIODeregisterBuffer(Address_BufferID);
		RIO().RIODeregisterBuffer(Data_BufferID);
		//	Cleanup other memory
		//delete[] uncompressed_data;
		delete Overlapped;
		/*while (!Data_Buffers.empty())
		{
			PRIO_BUF_EXT Buff = Data_Buffers.front();
			delete Buff->pAddrBuff;
			delete Buff;
			Data_Buffers.pop();
		}*/
		delete Address_Buffer;
		delete Data_Buffer;

		//	Cleanup our NetAddress
		delete Address;
		printf("\tSocket Closed\n");
	}
}