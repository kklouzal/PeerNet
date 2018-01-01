#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>	// IOCP functions and HANDLE
#include <stack>		// std::stack
#include <deque>		// std::deque
#include <thread>		// std::thread
#include <mutex>		// std::mutex

enum COMPLETION_KEY_SEND
{
	CK_STOP_SEND = 0,	//	Used to break a threads main loop
	CK_RIO_SEND = 1,	//	RIO Send completions
	CK_SEND = 2,		//	Used to initiate a send
};

class ConcurrentDeque;

//	Send Data Buffer Struct
//	Holds a pointer to the data buffer pool this send used
struct RIO_BUF_SEND : public RIO_BUF {
	//	Originating container for this buffer
	ConcurrentDeque* BufferContainer;
}; typedef RIO_BUF_SEND* PRIO_BUF_SEND;

//	Single Consumer Multi Producer concurrent deque manager
class ConcurrentDeque {
	std::deque<PRIO_BUF_SEND> Buffers;
	std::mutex BufferMutex;
public:
	//	Push a buffer back into the container
	inline void Push(PRIO_BUF_SEND Buffer) {
		BufferMutex.lock();
		Buffers.push_back(Buffer);
		BufferMutex.unlock();
	}

	//	Pull a buffer removing it from the container
	inline PRIO_BUF_SEND Pull() {
		PRIO_BUF_SEND Buffer = nullptr;
		BufferMutex.lock();
		Buffer = Buffers.front();
		Buffers.pop_front();
		BufferMutex.unlock();
		return Buffer;
	}

	//	Cleanup
	inline void Cleanup() {
		while (!Buffers.empty())
		{
			PRIO_BUF_SEND Buff = Buffers.front();
			delete Buff;
			Buffers.pop_front();
		}
	}
};

class ThreadPoolSend
{
	PeerNet::PeerNet* _PeerNet;
	RIO_EXTENSION_FUNCTION_TABLE _RIO;
	const unsigned char MaxThreads;
	const HANDLE IOCompletionPort;
	OVERLAPPED Overlap;
	RIO_CQ CompletionQueue;
	RIO_RQ RequestQueue;
	std::mutex* RioMutex;
	std::stack<thread> Threads;
	//	Data Buffer
	RIO_BUFFERID Data_BufferID;
	PCHAR const Data_Buffer;

	inline void ShutdownThreads() {
		//	Post a CK_STOP for each created thread
		for (unsigned char i = 0; i < MaxThreads; i++) {
			PostCompletion(CK_STOP_SEND);
		}
		//	Wait for each thread to exit
		while (!Threads.empty()) { Threads.top().join(); Threads.pop(); }
	}
public:

	inline RIO_CQ GetCompletionQueue() {
		return CompletionQueue;
	}

	inline void Initialize(RIO_RQ RQ) {
		RequestQueue = RQ;
		if (_RIO.RIONotify(CompletionQueue) != ERROR_SUCCESS) { printf("\tRIO Send Notify Failed\n"); return; }
	}

	//	Constructor
	inline ThreadPoolSend(PeerNet::PeerNet* PN, RIO_RQ RQ, std::mutex* RQMutex)
		: _PeerNet(PN), _RIO(PN->RIO()), MaxThreads(thread::hardware_concurrency()),
		IOCompletionPort(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MaxThreads)),
		Overlap(), RequestQueue(RQ), RioMutex(RQMutex), Threads(),
		Data_Buffer(new char[PN_MaxPacketSize*PN_MaxSendPackets]) {
		printf("\tOpening %i Send Threads\n", MaxThreads);

		//	Create Receive Completion Type and Queue
		RIO_NOTIFICATION_COMPLETION Completion;
		Completion.Type = RIO_IOCP_COMPLETION;
		Completion.Iocp.IocpHandle = IOCompletionPort;
		Completion.Iocp.CompletionKey = (void*)CK_RIO_SEND;
		Completion.Iocp.Overlapped = &Overlap;
		CompletionQueue = _RIO.RIOCreateCompletionQueue(PN_MaxSendPackets, &Completion);
		if (CompletionQueue == RIO_INVALID_CQ) { printf("Create Send Completion Queue Failed: %i\n", WSAGetLastError()); }

		//	Register Data Memory Buffer
		Data_BufferID = _RIO.RIORegisterBuffer(Data_Buffer, PN_MaxPacketSize*PN_MaxSendPackets);
		if (Data_BufferID == RIO_INVALID_BUFFERID) { printf("Send Data_Buffer: Invalid BufferID\n"); }

		//	Create our threads
		ULONG SendOffset = 0;
		for (unsigned char i = 0; i < MaxThreads; i++) {
			Threads.emplace(thread([&]() {
				ConcurrentDeque* Buffers = new ConcurrentDeque;
				//	Fill our send buffers
				const unsigned long BufferCount = PN_MaxSendPackets / MaxThreads;
				unsigned long CurBuffer = 0;
				while (CurBuffer < BufferCount)
				{
					//
					PRIO_BUF_SEND pBuf = new RIO_BUF_SEND;
					pBuf->BufferId = Data_BufferID;
					pBuf->Offset = SendOffset;
					pBuf->Length = PN_MaxPacketSize;
					pBuf->BufferContainer = Buffers;
					//	Save our Receive buffer so it can be cleaned up when the socket is destroyed
					Buffers->Push(pBuf);
					//	Increment counters
					SendOffset += PN_MaxPacketSize;
					CurBuffer++;
				}
				//
				RIORESULT CompletionResults[RIO_ResultsPerThread];
				DWORD numberOfBytes = 0;	//	Unused
				ULONG_PTR completionKey = 0;
				LPOVERLAPPED pOverlapped = nullptr;
				//	ZStd
				char*const Uncompressed_Data = new char[PN_MaxPacketSize];
				ZSTD_CCtx*const Compression_Context = ZSTD_createCCtx();

				//	Lock our thread to its own core
				SetThreadAffinityMask(GetCurrentThread(), i);
				//	Set our scheduling priority
				SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

				//	Run this threads main loop
				while (true) {
					//	Grab the next available completion or block until one arrives
					GetQueuedCompletionStatus(IOCompletionPort, &numberOfBytes, &completionKey, &pOverlapped, INFINITE);
					//	Process our completion
					switch (completionKey)
					{
					case CK_STOP_SEND: return;	// Break our main loop on CK_STOP
						//
						//	Finish Sending Event
					case CK_RIO_SEND:
					{
						const ULONG NumResults = _RIO.RIODequeueCompletion(CompletionQueue, CompletionResults, RIO_ResultsPerThread);
#ifdef NDEBUG
						_RIO.RIONotify(CompletionQueue);
#else
						if (RIO.RIONotify(CompletionQueue_Send) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }
						if (RIO_CORRUPT_CQ == NumResults) { printf("RIO Corrupt Results\n"); return; }
#endif
						//	Actually read the data from each received packet
						for (ULONG CurResult = 0; CurResult < NumResults; CurResult++)
						{
							//	Get the raw packet data into our buffer
							PRIO_BUF_SEND pBuffer = reinterpret_cast<PRIO_BUF_SEND>(CompletionResults[CurResult].RequestContext);
							//	Send this pBuffer back to the correct Thread Environment
							pBuffer->BufferContainer->Push(pBuffer);
						}
					}
					break;

					//
					//	Start Sending Event
					case CK_SEND:
					{
						const PRIO_BUF_SEND pBuffer = Buffers->Pull();
						//	If we are out of buffers push the request back out for another thread to pick up
						if (pBuffer == nullptr) { PostCompletion(CK_SEND, pOverlapped); return; }

						PeerNet::SendPacket* OutPacket = static_cast<PeerNet::SendPacket*>(pOverlapped);

						//	Compress our outgoing packets data payload into the rest of the data buffer
						pBuffer->Length = (ULONG)ZSTD_compressCCtx(Compression_Context,
							&Data_Buffer[pBuffer->Offset], PN_MaxPacketSize, OutPacket->GetData()->str().c_str(), OutPacket->GetData()->str().size(), 1);

						//	If compression was successful, actually transmit our packet
						if (pBuffer->Length > 0) {
							//printf("Compressed: %i->%i\n", SendPacket->GetData().size(), pBuffer->Length);
#ifdef _PERF_SPINLOCK
							while (!RioMutex_Send.try_lock()) {}
#else
							RioMutex->lock();
#endif
							_RIO.RIOSendEx(RequestQueue, pBuffer, 1, NULL, OutPacket->GetAddress(), NULL, NULL, NULL, pBuffer);
							RioMutex->unlock();
						}
						else { printf("Packet Compression Failed - %i\n", pBuffer->Length); }
						//	Mark packet as not sending
						OutPacket->IsSending.store(0);
						//	Cleanup managed SendPackets
						if (OutPacket->GetManaged())
						{
							//delete OutPacket;
							OutPacket->NeedsDelete.store(1);
						}
					}
					break;

					default: printf("Send Thread - Unknown Completion Key\n");
					}
				} // Close While Loop

				//	Cleanup our buffers
				Buffers->Cleanup();
				delete Buffers;

				//	Cleanup ZStd
				ZSTD_freeCCtx(Compression_Context);
				delete[] Uncompressed_Data;
			}));
		}
	}

	//	Destructor
	inline ~ThreadPoolSend() {
		//	Shutdown our threads
		ShutdownThreads();
		//	Close the completion queue
		_RIO.RIOCloseCompletionQueue(CompletionQueue);
		//	Deregister the data buffer
		_RIO.RIODeregisterBuffer(Data_BufferID);
		delete Data_Buffer;
		//	Close the IO Completion Port
		CloseHandle(IOCompletionPort);
		printf("\tClosed Send Thread Pool\n");
	}

	inline void PostCompletion(const ULONG_PTR Key, LPOVERLAPPED OutPacket = NULL) const {
		if (PostQueuedCompletionStatus(IOCompletionPort, NULL, Key, OutPacket) == 0) {
			printf("PostQueuedCompletionStatus Error: %i\n", GetLastError());
		}
	}

	inline const auto IOCP() const { return IOCompletionPort; }
	inline const auto& HardwareConcurrency() const { return MaxThreads; }
};