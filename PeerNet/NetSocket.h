#pragma once
#include "ThreadPool.hpp"

namespace PeerNet
{
	class ThreadEnvironment;

	//	Data Buffer Struct
	//	Holds raw data for a packet and it's address buffer
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
		void OnCompletion(ThreadEnvironment*const Env, const DWORD& numberOfBytes, const ULONG_PTR completionKey, OVERLAPPED*const pOverlapped);

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