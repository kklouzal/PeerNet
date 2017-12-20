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
protected:
	const unsigned char MaxThreads;
	const HANDLE IOCompletionPort;
	unordered_map<unsigned char, T*const> Environments;
	stack<thread> Threads;

private:
	virtual inline void OnCompletion(T*const ThreadEnv, const DWORD& numberOfBytes, const ULONG_PTR completionKey, OVERLAPPED*const pOverlapped) = 0;
public:
	auto const GetThreadEnv(const unsigned char& ThreadNum) const { return Environments.at(ThreadNum); }

	//	Constructor
	ThreadPoolIOCP() : MaxThreads(thread::hardware_concurrency()),
		IOCompletionPort(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MaxThreads)),
		Environments(), Threads() {
		printf("\tOpening %i Threads\n", MaxThreads);
		//	Create our threads
		for (unsigned char i = 0; i < MaxThreads; i++)
		{
			Environments.insert(std::make_pair(i, new T(MaxThreads)));

			Threads.emplace(thread([&]() {
				T*const MyEnv = Environments[i];
				DWORD numberOfBytes = 0;
				ULONG_PTR completionKey = 0;
				OVERLAPPED* pOverlapped = 0;

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
					OnCompletion(MyEnv, numberOfBytes, completionKey, pOverlapped);
				}}));
		}
	}

	//	Destructor
	~ThreadPoolIOCP() {
		//	Close the IO Completion Port
		CloseHandle(IOCompletionPort);
		printf("\tClose Thread Pool\n");
	}

	void ShutdownThreads()
	{
		//	Post a CK_STOP for each created thread
		for (unsigned char i = 0; i < MaxThreads; i++)
		{
			PostCompletion(CK_STOP);
		}
		//	Wait for each thread to exit
		while (!Threads.empty()) { Threads.top().join(); Threads.pop(); }
	}

	inline void PostCompletion(const ULONG_PTR Key) const {
		if (!PostQueuedCompletionStatus(IOCompletionPort, NULL, Key, NULL))
		{
			printf("PostQueuedCompletionStatus Error: %i\n", GetLastError());
		}
	}

	template <typename T>
	inline void PostCompletion(const ULONG_PTR Key, T OverlappedData) const {
		if (!PostQueuedCompletionStatus(IOCompletionPort, NULL, Key, reinterpret_cast<LPOVERLAPPED>(OverlappedData)))
		{
			printf("PostQueuedCompletionStatus Error: %i\n", GetLastError());
		}
	}

	const auto IOCP() const { return IOCompletionPort; }
	const auto& HardwareConcurrency() const { return MaxThreads; }
};