#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>	// IOCP functions and HANDLE
#include <stack>		// std::stack
#include <deque>		// std::deque
#include <thread>		// std::thread
#include <mutex>		// std::mutex

enum COMPLETION_KEY_RECV
{
	CK_STOP_RECV = 0,	//	used to break a threads main loop
	CK_RIO_RECV = 1,	//	RIO Receive completions
};

//	Receive Data Buffer Struct
//	Holds a pointer to the senders address buffer
struct RIO_BUF_RECV : public RIO_BUF {
	PRIO_BUF pAddrBuff;	//	Address Buffer for this data buffer
}; typedef RIO_BUF_RECV* PRIO_BUF_RECV;

class ThreadPoolReceive
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
	std::deque<PRIO_BUF_RECV> Buffers;
	//	Address Buffer
	RIO_BUFFERID Address_BufferID;
	PCHAR Address_Buffer;
	//	Data Buffer
	RIO_BUFFERID Data_BufferID;
	PCHAR const Data_Buffer;

	inline void ShutdownThreads() {
		//	Post a CK_STOP for each created thread
		for (unsigned char i = 0; i < MaxThreads; i++) {
			PostCompletion(CK_STOP_RECV);
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
		for (auto pBuf : Buffers)
		{
			if (!_RIO.RIOReceiveEx(RequestQueue, pBuf, 1, NULL, pBuf->pAddrBuff, NULL, NULL, NULL, pBuf))
			{
				printf("RIO Receive Failed %i\n", WSAGetLastError());
			}
		}
		if (_RIO.RIONotify(CompletionQueue) != ERROR_SUCCESS) { printf("\tRIO Receive Notify Failed\n"); return; }
	}

	//	Constructor
	inline ThreadPoolReceive(PeerNet::PeerNet* PN, RIO_RQ RQ, std::mutex* RQMutex)
		: _PeerNet(PN), _RIO(PN->RIO()), MaxThreads(thread::hardware_concurrency()),
		IOCompletionPort(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MaxThreads)),
		Overlap(), RequestQueue(RQ), RioMutex(RQMutex), Threads(),
		Address_Buffer(new char[sizeof(SOCKADDR_INET)*PN_MaxReceivePackets]),
		Data_Buffer(new char[PN_MaxPacketSize*PN_MaxReceivePackets]) {
		printf("\tOpening %i Receive Threads\n", MaxThreads);

		//	Create Receive Completion Type and Queue
		RIO_NOTIFICATION_COMPLETION Completion;
		Completion.Type = RIO_IOCP_COMPLETION;
		Completion.Iocp.IocpHandle = IOCompletionPort;
		Completion.Iocp.CompletionKey = (void*)CK_RIO_RECV;
		Completion.Iocp.Overlapped = &Overlap;
		CompletionQueue = _RIO.RIOCreateCompletionQueue(PN_MaxReceivePackets, &Completion);
		if (CompletionQueue == RIO_INVALID_CQ) { printf("Create Receive Completion Queue Failed: %i\n", WSAGetLastError()); }

		//	Register Address Memory Buffer
		Address_BufferID = _RIO.RIORegisterBuffer(Address_Buffer, sizeof(SOCKADDR_INET)*PN_MaxReceivePackets);
		if (Address_BufferID == RIO_INVALID_BUFFERID) { printf("Receive Address_Buffer: Invalid BufferID\n"); }

		//	Register Data Memory Buffer
		Data_BufferID = _RIO.RIORegisterBuffer(Data_Buffer, PN_MaxPacketSize*PN_MaxReceivePackets);
		if (Data_BufferID == RIO_INVALID_BUFFERID) { printf("Receive Data_Buffer: Invalid BufferID\n"); }

		//	Fill our receive buffers
		unsigned long CurBuffer = 0;
		unsigned long AddressOffset = 0;
		unsigned long ReceiveOffset = 0;
		while (CurBuffer < PN_MaxReceivePackets)
		{
			PRIO_BUF_RECV pBuf = new RIO_BUF_RECV;
			pBuf->BufferId = Data_BufferID;
			pBuf->Offset = ReceiveOffset;
			pBuf->Length = PN_MaxPacketSize;
			//
			pBuf->pAddrBuff = new RIO_BUF;
			pBuf->pAddrBuff->BufferId = Address_BufferID;
			pBuf->pAddrBuff->Offset = AddressOffset;
			pBuf->pAddrBuff->Length = sizeof(SOCKADDR_INET);
			//	Save our Receive buffer so it can be cleaned up when the socket is destroyed
			Buffers.push_back(pBuf);
			//	Increment counters
			ReceiveOffset += PN_MaxPacketSize;
			AddressOffset += sizeof(SOCKADDR_INET);
			CurBuffer++;
		}

		//	Create our threads
		for (unsigned char i = 0; i < MaxThreads; i++) {
			Threads.emplace(thread([&]() {
				RIORESULT CompletionResults[RIO_ResultsPerThread];
				DWORD numberOfBytes = 0;	//	Unused
				ULONG_PTR completionKey = 0;
				LPOVERLAPPED pOverlapped = nullptr;
				//	ZStd
				char*const Uncompressed_Data = new char[PN_MaxPacketSize];
				ZSTD_DCtx*const Decompression_Context = ZSTD_createDCtx();

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
					case CK_STOP_RECV: return;	// Break our main loop on CK_STOP
					case CK_RIO_RECV:
					{
						const ULONG NumResults = _RIO.RIODequeueCompletion(CompletionQueue, CompletionResults, RIO_ResultsPerThread);
#ifdef NDEBUG
						_RIO.RIONotify(CompletionQueue);
#else
						if (_RIO.RIONotify(CompletionQueue) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }
						if (RIO_CORRUPT_CQ == NumResults) { printf("RIO Corrupt Results\n"); return; }
#endif

						//	Actually read the data from each received packet
						for (ULONG CurResult = 0; CurResult < NumResults; CurResult++)
						{
							//	Get the raw packet data into our buffer
							const PRIO_BUF_RECV pBuffer = reinterpret_cast<PRIO_BUF_RECV>(CompletionResults[CurResult].RequestContext);

							const size_t DecompressResult = ZSTD_decompressDCtx(Decompression_Context,
								Uncompressed_Data, PN_MaxPacketSize, &Data_Buffer[pBuffer->Offset],
								CompletionResults[CurResult].BytesTransferred);

							//	Return if decompression fails
							//	TODO: Should be < 0; Will randomly crash at 0 though.
							if (DecompressResult < 1) {
								printf("Receive Packet - Decompression Failed!\n"); continue;
							}
							//	Grab
							//	"show" packet to peer for processing
							//	

							_PeerNet->TranslateData((SOCKADDR_INET*)&Address_Buffer[pBuffer->pAddrBuff->Offset], std::string(Uncompressed_Data, DecompressResult));

#ifdef _PERF_SPINLOCK
							while (!RioMutex_Receive.try_lock()) {}
#else
							RioMutex->lock();
#endif
							//	Push another read request into the queue
							if (!_RIO.RIOReceiveEx(RequestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, 0, pBuffer)) { printf("RIO Receive2 Failed\n"); }
							RioMutex->unlock();
						}
					}
					break;

					default: printf("Receive Thread - Unknown Completion Key\n");
					}
				} // Close While Loop

				//	Cleanup ZStd
				ZSTD_freeDCtx(Decompression_Context);
				delete[] Uncompressed_Data;
			}));
		}
	}

	//	Destructor
	inline ~ThreadPoolReceive() {
		//	Shutdown our threads
		ShutdownThreads();
		//	Close the completion queue
		_RIO.RIOCloseCompletionQueue(CompletionQueue);
		//	Deregister the address buffer
		_RIO.RIODeregisterBuffer(Address_BufferID);
		while (!Buffers.empty())
		{
			PRIO_BUF_RECV Buff = Buffers.front();
			delete Buff->pAddrBuff;
			delete Buff;
			Buffers.pop_front();
		}
		delete Address_Buffer;
		//	Deregister the data buffer
		_RIO.RIODeregisterBuffer(Data_BufferID);
		delete Data_Buffer;
		//	Close the IO Completion Port
		CloseHandle(IOCompletionPort);
		printf("\tClosed Receive Thread Pool\n");
	}

	inline void PostCompletion(const ULONG_PTR Key, LPOVERLAPPED OutPacket = NULL) const {
		if (PostQueuedCompletionStatus(IOCompletionPort, NULL, Key, OutPacket) == 0) {
			printf("PostQueuedCompletionStatus Error: %i\n", GetLastError());
		}
	}

	inline const auto IOCP() const { return IOCompletionPort; }
	inline const auto& HardwareConcurrency() const { return MaxThreads; }
};