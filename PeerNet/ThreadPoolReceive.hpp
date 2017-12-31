#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>		// IOCP functions and HANDLE
#include <stack>			// std::stack
#include <thread>			// std::thread
#include <unordered_map>	// std::unordered_map

using std::stack;
using std::thread;
using std::unordered_map;

enum COMPLETION_KEY
{
	CK_STOP = 0,	//	used to break a threads main loop
	CK_RIO_RECV = 1,	//	RIO Receive completions
};

class ThreadPoolReceive
{
	PeerNet::PeerNet* _PeerNet;
	RIO_EXTENSION_FUNCTION_TABLE _RIO;
	const unsigned char MaxThreads;
	const HANDLE IOCompletionPort;
	OVERLAPPED ReceiveOverlap;
	RIO_CQ CompletionQueue;
	std::mutex RioMutex;
	stack<thread> Threads;
public:

	//	Constructor
	inline ThreadPoolReceive(PeerNet::PeerNet* PN) : _PeerNet(PN), _RIO(PN->RIO()), MaxThreads(thread::hardware_concurrency()),
		IOCompletionPort(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MaxThreads)), ReceiveOverlap(), RioMutex(), Threads() {
		printf("\tOpening %i Receive Threads\n", MaxThreads);
		//	Create Receive Completion Type and Queue
		OVERLAPPED ReceiveOverlap;
		RIO_NOTIFICATION_COMPLETION Completion;
		Completion.Type = RIO_IOCP_COMPLETION;
		Completion.Iocp.IocpHandle = IOCompletionPort;
		Completion.Iocp.CompletionKey = (void*)CK_RIO_RECV;
		Completion.Iocp.Overlapped = &ReceiveOverlap;
		CompletionQueue = _RIO.RIOCreateCompletionQueue(PN_MaxReceivePackets, &Completion);
		if (CompletionQueue == RIO_INVALID_CQ) { printf("Create Completion Queue Failed: %i\n", WSAGetLastError()); }
		//	Create our threads
		for (unsigned char i = 0; i < MaxThreads; i++) {
			Threads.emplace(thread([&]() {
				T*const MyEnv = Environments[i];
				DWORD numberOfBytes = 0;	//	Unused
				ULONG_PTR completionKey = 0;
				LPOVERLAPPED pOverlapped = nullptr;

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
					case CK_STOP: return;	// Break our main loop on CK_STOP
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
								printf("Receive Packet - Decompression Failed!\n"); continue;
							}
							//	Grab
							//	"show" packet to peer for processing
							//	

							_PeerNet->TranslateData((SOCKADDR_INET*)&Address_Buffer[pBuffer->pAddrBuff->Offset], std::string(Env->Uncompressed_Data, DecompressResult));

#ifdef _PERF_SPINLOCK
							while (!RioMutex_Receive.try_lock()) {}
#else
							RioMutex_Receive.lock();
#endif
							//	Push another read request into the queue
							if (!RIO.RIOReceiveEx(RequestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, 0, pBuffer)) { printf("RIO Receive2 Failed\n"); }
							RioMutex_Receive.unlock();
						}
					}
					break;

					default: printf("Receive Thread - Unknown Completion Key\n");
					}
				}}));
		}
	}

	//	Destructor
	inline ~ThreadPoolReceive() {
		//	Close the IO Completion Port
		CloseHandle(IOCompletionPort);
		printf("\tClose Thread Pool\n");
	}

	inline void ShutdownThreads() {
		//	Post a CK_STOP for each created thread
		for (unsigned char i = 0; i < MaxThreads; i++) {
			PostCompletion(CK_STOP);
		}
		//	Wait for each thread to exit
		while (!Threads.empty()) { Threads.top().join(); Threads.pop(); }
	}

	inline void PostCompletion(const ULONG_PTR Key, LPOVERLAPPED OutPacket = NULL) const {
		if (PostQueuedCompletionStatus(IOCompletionPort, NULL, Key, OutPacket) == 0) {
			printf("PostQueuedCompletionStatus Error: %i\n", GetLastError());
		}
	}

	inline const auto IOCP() const { return IOCompletionPort; }
	inline const auto& HardwareConcurrency() const { return MaxThreads; }
};