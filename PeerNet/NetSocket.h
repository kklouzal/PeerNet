#pragma once
#include "ThreadPool.hpp"

#define PN_MaxPacketSize 1472		//	Max size of an outgoing or incoming packet
#define RIO_ResultsPerThread 128	//	How many results to dequeue from the stack per thread
#define PN_MaxSendPackets 14336		//	Max outgoing packets per socket before you run out of memory
#define PN_MaxReceivePackets 14336	//	Max pending incoming packets before new packets are disgarded

namespace PeerNet
{
	class ThreadEnvironment;

	//
	//	Data Buffer Struct
	//	Holds raw data for a packet and it's address buffer
	//
	struct RIO_BUF_EXT : public RIO_BUF
	{
		//	Owning ThreadEnvironment for this buffer
		ThreadEnvironment* MyEnv;
		//	Address Buffer for this data buffer
		PRIO_BUF pAddrBuff;
		//	Preallocated variables used in calls To GetQueuedCompletionStatus
		DWORD numberOfBytes = 0;
		ULONG_PTR completionKey = 0;
	}; typedef RIO_BUF_EXT* PRIO_BUF_EXT;

	//
	//	Thread Environment Class
	//	Each thread gets assigned an instance of this class
	//
	class ThreadEnvironment
	{
		std::queue<PRIO_BUF_EXT> Data_Buffers;
		std::mutex BuffersMutex;

	public:
		RIORESULT CompletionResults[RIO_ResultsPerThread];
		char*const Uncompressed_Data;
		ZSTD_CCtx*const Compression_Context;
		ZSTD_DCtx*const Decompression_Context;
		const unsigned int ThreadsInPool;
		inline ThreadEnvironment(const unsigned int MaxThreads)
			: ThreadsInPool(MaxThreads), BuffersMutex(), Data_Buffers(), CompletionResults(),
			Uncompressed_Data(new char[PN_MaxPacketSize]),
			Compression_Context(ZSTD_createCCtx()),
			Decompression_Context(ZSTD_createDCtx()) {}

		inline ~ThreadEnvironment()
		{
			ZSTD_freeDCtx(Decompression_Context);
			ZSTD_freeCCtx(Compression_Context);
			delete[] Uncompressed_Data;
			while (!Data_Buffers.empty())
			{
				PRIO_BUF_EXT Buff = Data_Buffers.front();
				delete Buff;
				Data_Buffers.pop();
			}
		}

		//	Will only be called by this thread
		inline PRIO_BUF_EXT PopBuffer()
		{
			//	Always leave 1 buffer in the pool for each running thread
			//	Prevents popping the front buffer as it's being pushed
			//	Eliminates the need to lock this function
			BuffersMutex.lock();
			if (Data_Buffers.size() <= ThreadsInPool) { BuffersMutex.unlock(); return nullptr; }
			PRIO_BUF_EXT Buffer = Data_Buffers.front();
			Data_Buffers.pop();
			BuffersMutex.unlock();
			return Buffer;
		}
		//	Will be called by multiple threads
		inline void PushBuffer(PRIO_BUF_EXT Buffer)
		{
			BuffersMutex.lock();
			Data_Buffers.push(Buffer);
			BuffersMutex.unlock();
		}
	};

	//
	//	NetSocket Class
	//
	class NetSocket : public ThreadPoolIOCP<ThreadEnvironment>
	{
		PeerNet* _PeerNet = nullptr;
		NetAddress* Address = nullptr;
		SOCKET Socket;

		const DWORD SendsPerThread = PN_MaxSendPackets / MaxThreads;

		std::deque<PRIO_BUF_EXT> Recv_Buffers;

		std::mutex RioMutex_Send;
		std::mutex RioMutex_Receive;
		//	RIO Function Table
		RIO_EXTENSION_FUNCTION_TABLE RIO;

		// Completion Queues
		RIO_CQ CompletionQueue_Recv;
		RIO_CQ CompletionQueue_Send;
		OVERLAPPED* Overlapped_Recv;
		OVERLAPPED* Overlapped_Send;
		// Send/Receive Request Queue
		RIO_RQ RequestQueue;
		//	Address Buffer
		RIO_BUFFERID Address_BufferID;
		PCHAR const Address_Buffer;
		//	Data Buffer
		RIO_BUFFERID Data_BufferID;
		PCHAR const Data_Buffer;

		//
		//	Thread Completion Function
		//	Process Sends and Receives
		//	(Executed via the Thread Pool)
		//
		inline void OnCompletion(ThreadEnvironment*const Env, const DWORD& numberOfBytes, const ULONG_PTR completionKey, OVERLAPPED*const pOverlapped)
		{
			switch (completionKey)
			{
				//
				//	Message Received Event
			case CK_RIO_RECV:
			{
				const ULONG NumResults = RIO.RIODequeueCompletion(CompletionQueue_Recv, Env->CompletionResults, RIO_ResultsPerThread);
#ifdef NDEBUG
				RIO.RIONotify(CompletionQueue_Recv);
#else
				if (RIO.RIONotify(CompletionQueue_Recv) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }
				if (RIO_CORRUPT_CQ == NumResults) { printf("RIO Corrupt Results\n"); return; }
#endif

				//	Actually read the data from each received packet
				for (ULONG CurResult = 0; CurResult < NumResults; CurResult++)
				{
					//	Get the raw packet data into our buffer
					const PRIO_BUF_EXT pBuffer = reinterpret_cast<PRIO_BUF_EXT>(Env->CompletionResults[CurResult].RequestContext);

					const size_t DecompressResult = ZSTD_decompressDCtx(Env->Decompression_Context,
						Env->Uncompressed_Data, PN_MaxPacketSize, &Data_Buffer[pBuffer->Offset],
						Env->CompletionResults[CurResult].BytesTransferred);

					//	Return if decompression fails
					//	TODO: Should be < 0; Will randomly crash at 0 though.
					if (DecompressResult < 1) {
						printf("Receive Packet - Decompression Failed!\n"); return;
					}

					_PeerNet->TranslateData((SOCKADDR_INET*)&Address_Buffer[pBuffer->pAddrBuff->Offset], std::string(Env->Uncompressed_Data, DecompressResult));

#ifdef _PERF_SPINLOCK
					while (!RioMutex.try_lock()) {}
#else
					RioMutex_Receive.lock();
#endif
					//	Push another read request into the queue
					if (!RIO.RIOReceiveEx(RequestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, 0, pBuffer)) { printf("RIO Receive2 Failed\n"); }
					RioMutex_Receive.unlock();
				}
			}
			break;

			//
			//	Finish Sending Event
			case CK_RIO_SEND:
			{
				const ULONG NumResults = RIO.RIODequeueCompletion(CompletionQueue_Send, Env->CompletionResults, RIO_ResultsPerThread);
#ifdef NDEBUG
				RIO.RIONotify(CompletionQueue_Send);
#else
				if (RIO.RIONotify(CompletionQueue_Send) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }
				if (RIO_CORRUPT_CQ == NumResults) { printf("RIO Corrupt Results\n"); return; }
#endif
				//	Actually read the data from each received packet
				for (ULONG CurResult = 0; CurResult < NumResults; CurResult++)
				{
					//	Get the raw packet data into our buffer
					PRIO_BUF_EXT pBuffer = reinterpret_cast<PRIO_BUF_EXT>(Env->CompletionResults[CurResult].RequestContext);
					//	Send this pBuffer back to the correct Thread Environment
					pBuffer->MyEnv->PushBuffer(pBuffer);
				}
			}
			break;

			//
			//	Start Sending Event
			case CK_SEND:
			{
				SendPacket*const OutPacket = static_cast<SendPacket*const>(pOverlapped);
				const PRIO_BUF_EXT pBuffer = Env->PopBuffer();
				//	If we are out of buffers push the request back out for another thread to pick up
				if (pBuffer == nullptr) { PostCompletion<SendPacket*const>(CK_SEND, OutPacket); return; }

				//	Compress our outgoing packets data payload into the rest of the data buffer
				pBuffer->Length = (ULONG)ZSTD_compressCCtx(Env->Compression_Context,
					&Data_Buffer[pBuffer->Offset], PN_MaxPacketSize, OutPacket->GetData()->str().c_str(), OutPacket->GetData()->str().size(), 1);

				//	If compression was successful, actually transmit our packet
				if (pBuffer->Length > 0) {
					//printf("Compressed: %i->%i\n", SendPacket->GetData().size(), pBuffer->Length);
#ifdef _PERF_SPINLOCK
					while (!RioMutex.try_lock()) {}
#else
					RioMutex_Send.lock();
#endif
					RIO.RIOSendEx(RequestQueue, pBuffer, 1, NULL, OutPacket->GetAddress(), NULL, NULL, NULL, pBuffer);
					RioMutex_Send.unlock();
					//	Cleanup managed SendPackets
					if (OutPacket->GetManaged())
					{
						delete OutPacket;
					}
				}
				else { printf("Packet Compression Failed - %i\n", pBuffer->Length); }
			}
			break;
			}
		}

	public:

		//
		//	NetSocket Constructor
		//
		inline NetSocket(PeerNet* PNInstance, NetAddress* MyAddress) : _PeerNet(PNInstance), Address(MyAddress),
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
		inline ~NetSocket()
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
	};
}