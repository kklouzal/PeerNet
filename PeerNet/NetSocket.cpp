#include "PeerNet.h"
#include "lz4.h"

namespace PeerNet
{
	namespace
	{
		std::unordered_map<std::string, NetPeer*const> Peers;
		std::string Delimiter = ":";
	}

	//	Retrieve a NetPeer* from it's formatted address or create a new one
	NetPeer*const RetrievePeer(std::string FormattedAddress, NetSocket*const Socket)
	{
		auto Peer = Peers.find(FormattedAddress);
		if (Peer == Peers.end())
		{
			size_t pos = 0;
			std::string StrIP;
			while ((pos = FormattedAddress.find(Delimiter)) != std::string::npos) {
				StrIP = FormattedAddress.substr(0, pos);
				FormattedAddress.erase(0, pos + Delimiter.length());
			}
			auto NewPeer = new NetPeer(StrIP, FormattedAddress, Socket);
			Peers.emplace(NewPeer->FormattedAddress(), NewPeer);
			//	Send out our discovery request
			NewPeer->SendPacket(NewPeer->CreateNewPacket(PacketType::PN_Reliable));
			return NewPeer;
		}
		return Peer->second;
	}

	void NetSocket::OnCompletion(ThreadEnvironment*const Env, const DWORD numberOfBytes, const ULONG_PTR completionKey, OVERLAPPED*const pOverlapped)
	{
		//
		//	Thread Completion Function
		//	Process Sends and Receives
		//	(Executed via the Thread Pool)
		//

		switch (completionKey)
		{
		case CK_RIO:
		{
			const ULONG NumResults = g_rio.RIODequeueCompletion(CompletionQueue, Env->CompletionResults, 128);
			if (g_rio.RIONotify(CompletionQueue) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }
			if (RIO_CORRUPT_CQ == NumResults) { printf("RIO Corrupt Results\n"); return; }
			//	Actually read the data from each received packet
			for (ULONG CurResult = 0; CurResult < NumResults; CurResult++)
			{
				//	Get the raw packet data into our buffer
				PRIO_BUF_EXT pBuffer = reinterpret_cast<PRIO_BUF_EXT>(Env->CompletionResults[CurResult].RequestContext);

				switch (pBuffer->completionKey)
				{
				case CK_SEND:
				{
					//	Completion of a CK_SEND request
					//	The thread processing this completion may be different from the thread that executed the CK_SEND
					//	Send this pBuffer back to the correct Thread Environment
					GetThreadEnv(pBuffer->ThreadNumber)->PushBuffer(pBuffer);
				}
				break;

				case CK_RECEIVE:
				{
					//	Try to decompress the received data
					const int CompressResult = LZ4_decompress_safe(&Data_Buffer[pBuffer->Offset], Env->Uncompressed_Data, Env->CompletionResults[CurResult].BytesTransferred, PacketSize);

					//printf("Decompressed: %i->%i\n", CompletionResults[CurResult].BytesTransferred, CompressResult);

					if (CompressResult > 0) {
						//	Get which peer sent this data
						const std::string SenderIP(inet_ntoa(((SOCKADDR_INET*)&Address_Buffer[pBuffer->pAddrBuff->Offset])->Ipv4.sin_addr));
						const std::string SenderPort(std::to_string(ntohs(((SOCKADDR_INET*)&Address_Buffer[pBuffer->pAddrBuff->Offset])->Ipv4.sin_port)));
						//	Determine which peer this packet belongs to and immediatly pass it to them for processing.
						RetrievePeer(SenderIP + std::string(":") + SenderPort, this)->ReceivePacket(new NetPacket(std::string(Env->Uncompressed_Data, CompressResult)));
					}
					else { printf("\tPacket Decompression Failed\n"); }
#ifdef _PERF_SPINLOCK
					while (!RioMutex.try_lock()) {}
#else
					RioMutex_Receive.lock();
#endif
					//	Push another read request into the queue
					if (!g_rio.RIOReceiveEx(RequestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, 0, pBuffer)) { printf("RIO Receive2 Failed\n"); }
					RioMutex_Receive.unlock();
				}
				break;

				default:
					printf("Unhandled RIO Key: %i\n", (int)pBuffer->completionKey);
				}
			}
		}
		break;

		case CK_SEND:
		{
			NetPacket*const SendPacket = reinterpret_cast<NetPacket*const>(pOverlapped);
			PRIO_BUF_EXT pBuffer = Env->PopBuffer();

			//	If we are out of buffers push the request back out for another thread to pick up
			if (pBuffer == nullptr) { PostCompletion<NetPacket*const>(CK_SEND, SendPacket); break; }
			pBuffer->Length = LZ4_compress_default(SendPacket->GetData().c_str(), &Data_Buffer[pBuffer->Offset], (int)SendPacket->GetDataSize(), PacketSize);
			if (pBuffer->Length > 0) {
				//printf("Compressed: %i->%i\n", SendPacket->GetData().size(), pBuffer->Length);
				std::memcpy(&Address_Buffer[pBuffer->pAddrBuff->Offset], SendPacket->GetPeer()->SockAddr(), sizeof(SOCKADDR_INET));
				//	Instead of just waiting here spinning our wheels,
				//	If we can't lock, add this send request into a queue and continue the thread
				//	If we CAN lock, process our queue THEN process this request.
				//	This will allow us to minimize contention and minimize wasted CPU cycles
#ifdef _PERF_SPINLOCK
				while (!RioMutex.try_lock()) {}
#else
				RioMutex_Send.lock();
#endif
				g_rio.RIOSendEx(RequestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, NULL, pBuffer);
				RioMutex_Send.unlock();
			}
			else { printf("Packet Compression Failed - %i\n", pBuffer->Length); }
		}
		break;
		}

		//
		//	End Thread Completion Function
		//
	}

	NetSocket::NetSocket(const std::string StrIP, const std::string StrPort) : Address(new NetAddress(StrIP, StrPort)),
		Address_Buffer(new char[sizeof(SOCKADDR_INET)*(MaxSends + MaxReceives)]),
		Data_Buffer(new char[PacketSize*(MaxSends + MaxReceives)]), Overlapped(new OVERLAPPED()),
		Socket(WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL, WSA_FLAG_REGISTERED_IO)),
		RioMutex_Send(), RioMutex_Receive(),
		ThreadPoolIOCP()
	{
		//	Make sure our socket was created properly
		if (Socket == INVALID_SOCKET) { printf("Socket Failed(%i)\n", WSAGetLastError()); }

		//	Bind our servers socket so we can listen for data
		if (bind(Socket, Address->AddrInfo()->ai_addr, (int)Address->AddrInfo()->ai_addrlen) == SOCKET_ERROR) { printf("Bind Failed(%i)\n", WSAGetLastError()); }

		//	Initialize RIO on this socket
		GUID functionTableID = WSAID_MULTIPLE_RIO;
		DWORD dwBytes = 0;
		if (WSAIoctl(Socket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
			&functionTableID,
			sizeof(GUID),
			(void**)&g_rio,
			sizeof(g_rio),
			&dwBytes, 0, 0) == SOCKET_ERROR) {
			printf("RIO Failed(%i)\n", WSAGetLastError());
		}

		//	Create Completion Queue
		RIO_NOTIFICATION_COMPLETION CompletionType;
		CompletionType.Type = RIO_IOCP_COMPLETION;
		CompletionType.Iocp.IocpHandle = this->IOCP();
		CompletionType.Iocp.CompletionKey = (void*)CK_RIO;
		CompletionType.Iocp.Overlapped = Overlapped;
		CompletionQueue = g_rio.RIOCreateCompletionQueue(MaxSends + MaxReceives, &CompletionType);
		if (CompletionQueue == RIO_INVALID_CQ) { printf("Create Completion Queue Failed: %i\n", WSAGetLastError()); }

		//	Create Request Queue
		RequestQueue = g_rio.RIOCreateRequestQueue(Socket, MaxReceives, 1, MaxSends, 1, CompletionQueue, CompletionQueue, NULL);
		if (RequestQueue == RIO_INVALID_RQ) { printf("Request Queue Failed: %i\n", WSAGetLastError()); }

		//	Initialize Address Memory Buffer
		Address_BufferID = g_rio.RIORegisterBuffer(Address_Buffer, sizeof(SOCKADDR_INET)*(MaxSends + MaxReceives));
		if (Address_BufferID == RIO_INVALID_BUFFERID) { printf("Address_Buffer: Invalid BufferID\n"); }

		//	Initialize Data Memory Buffer
		Data_BufferID = g_rio.RIORegisterBuffer(Data_Buffer, PacketSize*(MaxSends+MaxReceives));
		if (Data_BufferID == RIO_INVALID_BUFFERID) { printf("Data_Buffer: Invalid BufferID\n"); }

		//	Split Data Buffer into chunks and construct a new RIO_BUF_EXT for each slice
		//	Take a corresponding slice from our Address Buffer and construct a regular RIO_BUF
		//	Queue up the amount of receives specified in our constructor
		//	The left overs will be reserved for sends
		for (DWORD i = 0, ReceiveOffset = 0, AddressOffset = 0; i < (MaxSends+MaxReceives); i++)
		{
			PRIO_BUF_EXT pBuf = new RIO_BUF_EXT;
			pBuf->BufferId = Data_BufferID;
			pBuf->Offset = ReceiveOffset;
			pBuf->Length = PacketSize;
			pBuf->pAddrBuff = new RIO_BUF;
			pBuf->pAddrBuff->BufferId = Address_BufferID;
			pBuf->pAddrBuff->Offset = AddressOffset;
			pBuf->pAddrBuff->Length = sizeof(SOCKADDR_INET);

			pBuf->ThreadNumber = (unsigned char)floor(i / SendsPerThread);

			ReceiveOffset += PacketSize;
			AddressOffset += sizeof(SOCKADDR_INET);

			if (i < MaxSends)
			{
				//	This buffer will be used for sends so add it to our queue
				pBuf->completionKey = CK_SEND;
				//	Figure out which thread we will belong to
				pBuf->MyEnv = GetThreadEnv(pBuf->ThreadNumber);
				pBuf->MyEnv->PushBuffer(pBuf);
			} else {
				pBuf->completionKey = CK_RECEIVE;
				if (!g_rio.RIOReceiveEx(RequestQueue, pBuf, 1, NULL, pBuf->pAddrBuff, NULL, NULL, NULL, pBuf))
				{
					printf("RIO Receive %i Failed %i\n", (int)i, WSAGetLastError());
				}
			}
		}
		if (g_rio.RIONotify(CompletionQueue) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }

		//	Set Priority
		printf("\tListening On - %s\n", Address->FormattedAddress());
	}

	//	NetSocket Destructor
	//
	NetSocket::~NetSocket()
	{
		printf("\tClose Socket - %s\n", Address->FormattedAddress());
		shutdown(Socket, SD_BOTH);		//	Prohibit Socket from conducting any more Sends or Receives
		this->ShutdownThreads();		//	Shutdown the threads in our Thread Pool
		closesocket(Socket);			//	Shutdown Socket
										//	Cleanup our Completion Queue
		g_rio.RIOCloseCompletionQueue(CompletionQueue);
		//	Cleanup our RIO Buffers
		g_rio.RIODeregisterBuffer(Address_BufferID);
		g_rio.RIODeregisterBuffer(Data_BufferID);
		//	Cleanup other memory
		//delete[] uncompressed_data;
		delete Overlapped;
		/*while (!Data_Buffers.empty())
		{
			PRIO_BUF_EXT Buff = Data_Buffers.front();
			delete Buff->pAddrBuff;
			delete Buff;
			Data_Buffers.pop();
		}*/
		delete Address_Buffer;
		delete Data_Buffer;

		//	Cleanup our NetAddress
		delete Address;
		printf("\tSocket Closed\n");
	}
}