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
	CK_STOP			=	0,	//	used to break a threads main loop
	CK_RIO_RECV		=	1,	//	RIO Receive completions
	CK_RIO_SEND		=	2,	//	RIO Send completions
	CK_SEND			=	3,	//	used during send operation
	CK_RECEIVE		=	4	//	used during receive operation
};

template <typename T>
class ThreadPoolIOCP
{
	const HANDLE IOCompletionPort;
	unordered_map<unsigned char, T*const> Environments;
	stack<thread> Threads;
	inline virtual void OnCompletion(T*const ThreadEnv, const ULONG_PTR completionKey, LPOVERLAPPED pOverlapped) = 0;
protected:
	const unsigned char MaxThreads;
public:
	inline auto const GetThreadEnv(const unsigned char& ThreadNum) const { return Environments.at(ThreadNum); }

	//	Constructor
	inline ThreadPoolIOCP() : MaxThreads(thread::hardware_concurrency()),
		IOCompletionPort(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MaxThreads)),
		Environments(), Threads() {
		printf("\tOpening %i Threads\n", MaxThreads);
		//	Create our threads
		for (unsigned char i = 0; i < MaxThreads; i++) {
			//	Create the environment
			Environments.emplace(i, new T(MaxThreads));
			//	Create the thread
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
					//	break our main loop on CK_STOP
					if (completionKey == CK_STOP) { delete MyEnv; return; }
					//	Call user defined completion function
					OnCompletion(MyEnv, completionKey, pOverlapped);
				}}));
		}
	}

	//	Destructor
	inline ~ThreadPoolIOCP() {
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