#pragma once

namespace PeerNet
{
	class ThreadEnvironment;

	struct RIO_BUF_EXT : public RIO_BUF
	{
		//	Reserved to alow RIO CK_SEND completions to cleanup its initiating NetPacket under certain circumstances
		SendPacket* MyNetPacket;

		ThreadEnvironment* MyEnv;

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
		RIORESULT CompletionResults[RIO_ResultsPerThread];
		char*const Uncompressed_Data;
		ZSTD_CCtx* Compression_Context;
		ZSTD_DCtx* Decompression_Context;
		const unsigned int ThreadsInPool;
		ThreadEnvironment(const unsigned int MaxThreads)
			: ThreadsInPool(MaxThreads), BuffersMutex(), Data_Buffers(), CompletionResults(),
			Uncompressed_Data(new char[PN_MaxPacketSize]),
			Compression_Context(ZSTD_createCCtx()),
			Decompression_Context(ZSTD_createDCtx()) {}

		~ThreadEnvironment()
		{
			ZSTD_freeDCtx(Decompression_Context);
			ZSTD_freeCCtx(Compression_Context);
			delete[] Uncompressed_Data;
			while (!Data_Buffers.empty())
			{
				PRIO_BUF_EXT Buff = Data_Buffers.front();
				delete Buff;
				Data_Buffers.pop();
			}
		}

		//	Will only be called by this thread
		PRIO_BUF_EXT PopBuffer()
		{
			//	Always leave 1 buffer in the pool for each running thread
			//	Prevents popping the front buffer as it's being pushed
			//	Eliminates the need to lock this function
			BuffersMutex.lock();
			if (Data_Buffers.size() <= ThreadsInPool) { BuffersMutex.unlock(); return nullptr; }
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
}