#pragma once
//#define WIN32_LEAN_AND_MEAN
//#include <Windows.h>		// IOCP functions and HANDLE
#include <stack>			// std::stack
#include <thread>			// std::thread
#include <functional>		// std::function
#include <unordered_map>	// std::unordered_map

using std::stack;
using std::thread;
using std::function;
using std::unordered_map;

enum COMPLETION_KEY
{
	CK_STOP			=	0,	//	used to break a threads main loop	(REQUIRED)
	CK_RIO			=	1,	//	used for RIO completions			(REQUIRED)
	CK_SEND			=	2,	//	used during send operation			(USER CUSTOM)
	CK_RECEIVE		=	3	//	used during receive operation		(USER CUSTOM)
};

template <typename T>
class ThreadPoolIOCP
{
protected:
	const HANDLE IOCompletionPort;
	const unsigned char MaxThreads;
	unordered_map<unsigned char, T*const> Environments;
	stack<thread> Threads;

private:
	virtual void OnCompletion(T*const ThreadEnv, const DWORD numberOfBytes, const ULONG_PTR completionKey, const OVERLAPPED*const pOverlapped) = 0;
public:
	T*const GetThreadEnv(const unsigned char ThreadNum) const { return Environments.at(ThreadNum); }

	//	Constructor
	ThreadPoolIOCP() :
		IOCompletionPort(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, NULL)),
		MaxThreads(thread::hardware_concurrency()), Environments(), Threads() {
		printf("Thread Pool Opening %i Threads\n", MaxThreads);
		//	Create our threads
		for (unsigned char i = 0; i < MaxThreads; i++)
		{
			printf("Creating IOCP Thread %i\n", i);

			Environments.insert(std::make_pair(i, new T()));
			//Environments[i]->ThreadNumber = i;

			Threads.emplace(thread([&]() {
				T*const MyEnv = Environments[i];
				DWORD numberOfBytes = 0;
				ULONG_PTR completionKey = 0;
				OVERLAPPED* pOverlapped = 0;

				//	Set our scheduling priority
				SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

				//	Run this threads main loop
				while (true) {
					//	Grab the next available completion or block until one arrives
					GetQueuedCompletionStatus(IOCompletionPort, &numberOfBytes, &completionKey, &pOverlapped, INFINITE);
					//	break our main loop on CK_STOP
					if (completionKey == CK_STOP) { printf("Stop Completion Received\n"); delete MyEnv; return; }
					//	Call user defined completion function
					OnCompletion(MyEnv, numberOfBytes, completionKey, pOverlapped);
				}}));
		}
	}

	//	Destructor
	~ThreadPoolIOCP() {
		//	Close the IO Completion Port
		CloseHandle(IOCompletionPort);
		printf("IOCP Thread Pool Closed\n");
	}

	void ShutdownThreads()
	{
		//	Post a CK_STOP for each created thread
		for (unsigned char i = 0; i < MaxThreads; i++)
		{
			printf("Posting IOCP STOP Completion %i\n", i);
			PostCompletion(CK_STOP);
		}
		//	Wait for each thread to exit
		while (!Threads.empty()) { Threads.top().join(); Threads.pop(); }
	}

	void PostCompletion(const ULONG_PTR Key) const {
		if (!PostQueuedCompletionStatus(IOCompletionPort, NULL, Key, NULL))
		{
			printf("PostQueuedCompletionStatus Error: %i\n", GetLastError());
			//exit(0);	//	Terminate the application
		}
	}

	template <typename T>
	void PostCompletion(const ULONG_PTR Key, T OverlappedData) const {
		if (!PostQueuedCompletionStatus(IOCompletionPort, NULL, Key, reinterpret_cast<LPOVERLAPPED>(OverlappedData)))
		{
			printf("PostQueuedCompletionStatus Error: %i\n", GetLastError());
			//exit(0);	//	Terminate the application
		}
	}

	const auto IOCP() const { return IOCompletionPort; }
	const auto HardwareConcurrency() const { return MaxThreads; }
};