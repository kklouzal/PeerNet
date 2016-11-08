#pragma once
#include "ThreadPool.hpp"

namespace PeerNet
{
	class ThreadEnvironment;

	struct RIO_BUF_EXT : public RIO_BUF
	{
		//	Reserved to alow RIO CK_SEND completions to cleanup its initiating NetPacket under certain circumstances
		NetPacket* NetPacket;

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
		RIORESULT CompletionResults[128];
		char*const Uncompressed_Data;
		ThreadEnvironment() : BuffersMutex(), Data_Buffers(), CompletionResults(), Uncompressed_Data(new char[1472]) {}
		~ThreadEnvironment() { delete[] Uncompressed_Data; }

		//	Will only be called by this thread
		PRIO_BUF_EXT PopBuffer()
		{
			//	Always leave 1 buffer in the pool
			//	Prevents popping the front buffer as it's being pushed
			//	Eliminates the need to lock this function
			BuffersMutex.lock();
			if (Data_Buffers.empty()) { BuffersMutex.unlock(); return nullptr; }
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
		//	Maximum size of an individual packet in bytes
		const DWORD PacketSize = 1472;
		//	Maximum pending receive packets
		const DWORD MaxReceives = 1024;
		const DWORD MaxSends = 1024;

		const DWORD SendsPerThread = MaxSends / MaxThreads;

		const NetAddress*const Address;
		SOCKET Socket;
		RIO_EXTENSION_FUNCTION_TABLE g_rio;

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

		void OnCompletion(ThreadEnvironment*const Env, const DWORD numberOfBytes, const ULONG_PTR completionKey, OVERLAPPED*const pOverlapped);

	public:
		//	NetSocket Constructor
		//
		//	args	-	StrIP, StrPort	-	IP and Port this socket will bind to
		//
		//	Resolve the input address
		//	Create our Socket
		//	Initialize our Base Class (ThreadPoolIOCP) and use a lambda to specify each threads completion function
		NetSocket(const std::string StrIP, const std::string StrPort);
		~NetSocket();

	};

	//	Checks for an existing connected peer and returns it
	//	Or returns a newly constructed NetPeer and immediatly sends the discovery packet
	NetPeer*const RetrievePeer(const std::string FormattedAddress, NetSocket*const Socket);
}