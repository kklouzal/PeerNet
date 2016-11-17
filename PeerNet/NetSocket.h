#pragma once
#include "ThreadPool.hpp"

namespace PeerNet
{
	class ThreadEnvironment;

	struct RIO_BUF_EXT : public RIO_BUF
	{
		//	Reserved to alow RIO CK_SEND completions to cleanup its initiating NetPacket under certain circumstances
		SendPacket* MyNetPacket;

		ThreadEnvironment* MyEnv;
		unsigned char ThreadNumber;

		PRIO_BUF pAddrBuff;

		//	Values Filled Upon Call To GetQueuedCompletionStatus
		//	Unused and basically just a way to allocate these variables upfront and only once
		DWORD numberOfBytes = 0;
		ULONG_PTR completionKey = 0;
		//
	}; typedef RIO_BUF_EXT* PRIO_BUF_EXT;

	class ThreadEnvironment
	{
		std::queue<PRIO_BUF_EXT> Data_Buffers;
		std::mutex BuffersMutex;
	public:
		RIORESULT CompletionResults[RIO_ResultsPerThread];
		char*const Uncompressed_Data;
		const unsigned int ThreadsInPool;
		ThreadEnvironment(const unsigned int MaxThreads)
			: ThreadsInPool(MaxThreads), BuffersMutex(), Data_Buffers(), CompletionResults(), Uncompressed_Data(new char[PN_MaxPacketSize]) {}

		~ThreadEnvironment()
		{
			delete[] Uncompressed_Data;
			while (!Data_Buffers.empty())
			{
				PRIO_BUF_EXT Buff = Data_Buffers.front();
				delete Buff->pAddrBuff;
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
		const DWORD SendsPerThread = PN_MaxSendPackets / MaxThreads;

		std::deque<PRIO_BUF_EXT> Recv_Buffers;

		NetAddress* Address;
		SOCKET Socket;

		std::mutex RioMutex_Send;
		std::mutex RioMutex_Receive;

		// Completion Queue
		RIO_CQ CompletionQueue;
		OVERLAPPED* Overlapped;
		// Send/Receive Request Queue
		RIO_RQ RequestQueue;
		//	Address Buffer
		RIO_BUFFERID Address_BufferID;
		PCHAR const Address_Buffer;
		//	Data Buffer
		RIO_BUFFERID Data_BufferID;
		PCHAR const Data_Buffer;

		void OnCompletion(ThreadEnvironment*const Env, const DWORD numberOfBytes, const ULONG_PTR completionKey, OVERLAPPED* pOverlapped);

	public:
		NetSocket(NetAddress* MyAddress);
		~NetSocket();
	};
}