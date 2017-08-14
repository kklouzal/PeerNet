#pragma once
//#define WIN32_LEAN_AND_MEAN
//#include <Windows.h>		// IOCP functions and HANDLE
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
	CK_SEND_DELETE	=	4,	//	used during send operation (deletes the SendPacket*)
	CK_RECEIVE		=	5	//	used during receive operation
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
	virtual void OnCompletion(T*const ThreadEnv, const DWORD numberOfBytes, const ULONG_PTR completionKey, OVERLAPPED* pOverlapped) = 0;
public:
	auto const GetThreadEnv(const unsigned char ThreadNum) const { return Environments.at(ThreadNum); }

	//	Constructor
	//
	//	Allocate one third as many threads for the socket as the system physically has available
	//	odd values are rounded up
	//
	//	ToDo:
	//	Round-Down and ensure minimum value of 1.
	ThreadPoolIOCP() : MaxThreads(ceil(thread::hardware_concurrency()/3)),
		IOCompletionPort(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MaxThreads)),
		Environments(), Threads() {
		PeerNet::Log("\tOpening " + std::to_string(MaxThreads) + " Threads\n");
		//	Create our threads
		for (unsigned char i = 0; i < MaxThreads; i++)
		{
			Environments.insert(std::make_pair(i, new T(MaxThreads)));

			Threads.emplace(thread([&]() {
				T*const MyEnv = Environments[i];
				DWORD numberOfBytes = 0;
				ULONG_PTR completionKey = 0;
				OVERLAPPED* pOverlapped = 0;

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
		PeerNet::Log("\tClose Thread Pool\n");
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

	void PostCompletion(const ULONG_PTR Key) const {
		if (!PostQueuedCompletionStatus(IOCompletionPort, NULL, Key, NULL))
		{
			PeerNet::Log("PostQueuedCompletionStatus Error: " + std::to_string(GetLastError()) + "\n");
			//exit(0);	//	Terminate the application
		}
	}

	template <typename T>
	void PostCompletion(const ULONG_PTR Key, T OverlappedData) const {
		if (!PostQueuedCompletionStatus(IOCompletionPort, NULL, Key, reinterpret_cast<LPOVERLAPPED>(OverlappedData)))
		{
			PeerNet::Log("PostQueuedCompletionStatus Error: " + std::to_string(GetLastError()) + "\n");
			//exit(0);	//	Terminate the application
		}
	}

	const auto IOCP() const { return IOCompletionPort; }
	const auto HardwareConcurrency() const { return MaxThreads; }
};