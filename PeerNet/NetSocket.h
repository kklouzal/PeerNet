#pragma once
#include "ThreadPool.hpp"
#include "ThreadEnvironment.hpp"

namespace PeerNet
{

	class NetSocket : public ThreadPoolIOCP<ThreadEnvironment>
	{
		const DWORD SendsPerThread = PN_MaxSendPackets / MaxThreads;

		std::deque<PRIO_BUF_EXT> Recv_Buffers;

		NetAddress* Address;
		SOCKET Socket;

		std::mutex RioMutex_Send;
		std::mutex RioMutex_Receive;

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

		void OnCompletion(ThreadEnvironment*const Env, const DWORD numberOfBytes, const ULONG_PTR completionKey, OVERLAPPED* pOverlapped);

	public:
		NetSocket(NetAddress* MyAddress);
		~NetSocket();
	};
}