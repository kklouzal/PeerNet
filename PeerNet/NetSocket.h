#pragma once
#include "ThreadPool.hpp"

namespace PeerNet
{
	class ThreadEnvironment;

	//	Data Buffer Struct
	//	Holds raw data for a packet and it's address buffer
	struct RIO_BUF_EXT : public RIO_BUF
	{
		//	Reserved to alow RIO CK_SEND completions to cleanup its initiating NetPacket under certain circumstances
		SendPacket* MyNetPacket;
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
		ThreadEnvironment(const unsigned int MaxThreads)
			: ThreadsInPool(MaxThreads), BuffersMutex(), Data_Buffers(), CompletionResults(),
			Uncompressed_Data(new char[PN_MaxPacketSize]),
			Compression_Context(ZSTD_createCCtx()),
			Decompression_Context(ZSTD_createDCtx()) {}

		~ThreadEnvironment()
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
		PRIO_BUF_EXT PopBuffer()
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
		void PushBuffer(PRIO_BUF_EXT Buffer)
		{
			BuffersMutex.lock();
			Data_Buffers.push(Buffer);
			BuffersMutex.unlock();
		}
	};

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
			case CK_RIO_RECV:
			{
				const ULONG NumResults = RIO.RIODequeueCompletion(CompletionQueue_Recv, Env->CompletionResults, RIO_ResultsPerThread);
				if (RIO.RIONotify(CompletionQueue_Recv) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }
				if (RIO_CORRUPT_CQ == NumResults) { printf("RIO Corrupt Results\n"); return; }

				//	Actually read the data from each received packet
				for (ULONG CurResult = 0; CurResult < NumResults; CurResult++)
				{
					//	Get the raw packet data into our buffer
					const PRIO_BUF_EXT pBuffer = reinterpret_cast<PRIO_BUF_EXT>(Env->CompletionResults[CurResult].RequestContext);

					//	Determine which peer this packet belongs to and pass the data payload off to our NetPeer so they can decompress it according to the TypeID
					_PeerNet->GetPeer((SOCKADDR_INET*)&Address_Buffer[pBuffer->pAddrBuff->Offset])->Receive_Packet(
						&Data_Buffer[pBuffer->Offset],
						Env->CompletionResults[CurResult].BytesTransferred,
						PN_MaxPacketSize,
						Env->Uncompressed_Data,
						Env->Decompression_Context);
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

			case CK_RIO_SEND:
			{
				const ULONG NumResults = RIO.RIODequeueCompletion(CompletionQueue_Send, Env->CompletionResults, RIO_ResultsPerThread);
				if (RIO.RIONotify(CompletionQueue_Send) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }
				if (RIO_CORRUPT_CQ == NumResults) { printf("RIO Corrupt Results\n"); return; }
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
					}
					//	Send this pBuffer back to the correct Thread Environment
					pBuffer->MyEnv->PushBuffer(pBuffer);
				}
			}
			break;

			case CK_SEND:
			{
				SendPacket*const OutPacket = static_cast<SendPacket*>(pOverlapped);
				PRIO_BUF_EXT pBuffer = Env->PopBuffer();
				//	If we are out of buffers push the request back out for another thread to pick up
				if (pBuffer == nullptr) { PostCompletion<SendPacket*>(CK_SEND, OutPacket); return; }
				//	This will allow the CK_SEND RIO completion to cleanup SendPacket when IsManaged() == true
				if (OutPacket->GetManaged())
				{
					pBuffer->MyNetPacket = OutPacket;
					pBuffer->completionKey = CK_SEND_DELETE;
				}
				
				//	Compress our outgoing packets data payload into the rest of the data buffer
				pBuffer->Length = (ULONG)OutPacket->GetPeer()->CompressPacket(OutPacket, &Data_Buffer[pBuffer->Offset],
					PN_MaxPacketSize, Env->Compression_Context);

				//	If compression was successful, actually transmit our packet
				if (pBuffer->Length > 0) {
					//printf("Compressed: %i->%i\n", SendPacket->GetData().size(), pBuffer->Length);
#ifdef _PERF_SPINLOCK
					while (!RioMutex.try_lock()) {}
#else
					RioMutex_Send.lock();
#endif
					RIO.RIOSendEx(RequestQueue, pBuffer, 1, NULL, OutPacket->GetPeer()->GetAddress(), NULL, NULL, NULL, pBuffer);
					RioMutex_Send.unlock();
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
		NetSocket(PeerNet* PNInstance, NetAddress* MyAddress);

		//
		//	NetSocket Destructor
		//
		~NetSocket();
	};
}