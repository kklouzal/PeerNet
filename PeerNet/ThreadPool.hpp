#pragma once
#include <Windows.h>		// IOCP functions and HANDLE
#define WIN32_LEAN_AND_MEAN
#include <stack>			// std::stack
#include <thread>			// std::thread
#include <functional>		// std::function

using std::stack;
using std::thread;
using std::function;

enum COMPLETION_KEY
{
	CK_STOP = 0				// used to break a threads main loop
};

class ThreadPoolIOCP
{
	const function<void(const DWORD, const ULONG_PTR, const OVERLAPPED*const)> OnCompletion;
	const HANDLE IOCompletionPort;
	stack<thread> Threads;
	const int MaxThreads;

public:

	//	Constructor
	ThreadPoolIOCP(const function<void(const DWORD, const ULONG_PTR, const OVERLAPPED*const)> OnCompletionFunc) : OnCompletion(OnCompletionFunc),
		IOCompletionPort(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, NULL)),
		MaxThreads(thread::hardware_concurrency()) {
		printf("Thread Pool Opening %i Threads\n", MaxThreads);
		//	Create our threads
		for (unsigned char i = 0; i < MaxThreads; i++)
		{
			printf("Creating IOCP Thread %i\n", i);

			Threads.emplace(thread([&]() {
				DWORD numberOfBytes = 0;
				ULONG_PTR completionKey = 0;
				OVERLAPPED* pOverlapped = 0;

				//	Run this threads main loop
				while (true)
				{
					//	Grab the next available completion or block until one arrives
					if (!GetQueuedCompletionStatus(IOCompletionPort, &numberOfBytes, &completionKey, &pOverlapped, INFINITE))
					{
						printf("GetQueuedCompletionStatus Error: %i\n", GetLastError());
					}

					//	break our main loop on CK_STOP
					if (completionKey == CK_STOP) { printf("Stop Completion Received\n"); break; }

					//	Call user defined completion function
					OnCompletion(numberOfBytes, completionKey, pOverlapped);
				}
			}));
		}
	}

	//	Destructor
	~ThreadPoolIOCP() {
		//	Post a CK_STOP for each created thread
		for (unsigned char i = 0; i < MaxThreads; i++)
		{
			printf("Posting IOCP STOP Completion %i\n", i);
			PostCompletion(CK_STOP);
		}
		//	Wait for each thread to exit
		while (!Threads.empty()) { Threads.top().join(); Threads.pop(); }
		//	Close the IO Completion Port
		CloseHandle(IOCompletionPort);
		printf("IOCP Thread Pool Closed\n");
	}

	void PostCompletion(const ULONG_PTR Key) const {
		if (!PostQueuedCompletionStatus(IOCompletionPort, NULL, Key, NULL))
		{
			printf("PostQueuedCompletionStatus Error: %i\n", GetLastError());
			//exit(0);	//	Terminate the application
		}
	}
};