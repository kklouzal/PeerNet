#include "PeerNet.h"
#include "lz4.h"

namespace PeerNet
{
	namespace
	{
		std::mutex PeersMutex;
		std::unordered_map<std::string, NetPeer*const> Peers;
	}

	//	Retrieve a NetPeer* from it's formatted address or create a new one
	NetPeer*const RetrievePeer(const std::string FormattedAddress, NetSocket*const Socket)
	{
		PeersMutex.lock();
		if (Peers.count(FormattedAddress))
		{
			auto Peer = Peers.at(FormattedAddress);
			PeersMutex.unlock();
			return Peer;
		}
		else {
			size_t pos = 0;
			std::string Delimiter = ":";
			std::string StrPort = FormattedAddress;
			std::string StrIP;
			while ((pos = StrPort.find(Delimiter)) != std::string::npos) {
				StrIP = StrPort.substr(0, pos);
				StrPort.erase(0, pos + Delimiter.length());
			}
			auto Peer = new NetPeer(StrIP, StrPort, Socket);
			Peers.emplace(Peer->FormattedAddress(), Peer);
			PeersMutex.unlock();
			return Peer;
		}
	}

	//	Compresses and sends a packet
	void NetSocket::CompressAndSendPacket(PRIO_BUF_EXT pBuffer, const NetPacket*const SendPacket)
	{
		pBuffer->Length = LZ4_compress_default(SendPacket->GetData().c_str(), &Data_Buffer[pBuffer->Offset], SendPacket->GetData().size(), PacketSize);
		if (pBuffer->Length > 0) {
			//printf("Compressed: %i->%i\n", SendPacket->GetData().size(), pBuffer->Length);
			memcpy(&Address_Buffer[pBuffer->pAddrBuff->Offset], SendPacket->GetPeer()->SockAddr(), sizeof(SOCKADDR_INET));
			g_rio.RIOSendEx(RequestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, NULL, pBuffer);
		}
		else { printf("Packet Compression Failed - %i\n", pBuffer->Length); }
	}

	NetSocket::NetSocket(const std::string StrIP, const std::string StrPort) : Address(new NetAddress(StrIP, StrPort)),
		Address_Buffer(new char[sizeof(SOCKADDR_INET)*(MaxSends+MaxReceives)]),
		Data_Buffer(new char[PacketSize*(MaxSends+MaxReceives)]), Overlapped(new OVERLAPPED()),
		Socket(WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL, WSA_FLAG_REGISTERED_IO)),
		ThreadPoolIOCP([&](const DWORD numberOfBytes, const ULONG_PTR completionKey, const OVERLAPPED*const pOverlapped)
	{
		//
		//	Thread Completion Function
		//	Process Sends and Receives
		//	(Executed via the Thread Pool)
		//

		//	Need to lock access to Completion Queues? One queue per thread??
		//printf("\tNewCompletion\tIOCP Key: %i ", (int)completionKey);
		switch (completionKey)
		{
			case CK_RIO:
			{
				const ULONG NumResults = g_rio.RIODequeueCompletion(CompletionQueue, CompletionResults, (MaxSends+MaxReceives));
				if (RIO_CORRUPT_CQ == NumResults) { printf("RIO Corrupt Results - Deleting Socket\n"); return; }
				//	Actually read the data from each received packet
				for (ULONG CurResult = 0; CurResult < NumResults; CurResult++)
				{
					//	Get the raw packet data into our buffer
					PRIO_BUF_EXT pBuffer = reinterpret_cast<PRIO_BUF_EXT>(CompletionResults[CurResult].RequestContext);

					switch (pBuffer->completionKey)
					{
						case CK_SEND:
						{
							//	Return our data buffer
							std::unique_lock<std::mutex> DataLocker(DataMutex);
							Data_Buffers.push(pBuffer);
							DataLocker.unlock();
						}
						break;

						case CK_RECEIVE:
						{
							//	Try to decompress the received data
							char*const Uncompressed_Data = new char[PacketSize];
							const int CompressResult = LZ4_decompress_safe(&Data_Buffer[pBuffer->Offset], Uncompressed_Data, CompletionResults[CurResult].BytesTransferred, PacketSize);

							//printf("Decompressed: %i->%i\n", CompletionResults[CurResult].BytesTransferred, CompressResult);

							if (CompressResult > 0) {
								//	Get which peer sent this data
								const std::string SenderIP(inet_ntoa(((SOCKADDR_INET*)&Address_Buffer[pBuffer->pAddrBuff->Offset])->Ipv4.sin_addr));
								const std::string SenderPort(std::to_string(ntohs(((SOCKADDR_INET*)&Address_Buffer[pBuffer->pAddrBuff->Offset])->Ipv4.sin_port)));
								//	Determine which peer this packet belongs to and immediatly pass it to them for processing.
								RetrievePeer(SenderIP + std::string(":") + SenderPort, this)->ReceivePacket(new NetPacket(std::string(Uncompressed_Data, CompressResult)));
							} else { printf("\tPacket Decompression Failed\n"); }
							delete[] Uncompressed_Data;
							//	Push another read request into the queue
							if (!g_rio.RIOReceiveEx(RequestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, 0, pBuffer)) { printf("RIO Receive2 Failed\n"); }
						}
						break;

						default:
						printf("Unhandled RIO Key: %i\n", (int)pBuffer->completionKey);
					}
				}
				if (g_rio.RIONotify(CompletionQueue) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }
			}
			break;

			case CK_SEND:
			{
				//	Grab a data buffer
				std::unique_lock<std::mutex> DataLocker(DataMutex);
				if (Data_Buffers.empty()) { DataLocker.unlock(); break; }
				PRIO_BUF_EXT pBuffer = Data_Buffers.top();
				Data_Buffers.pop();
				DataLocker.unlock();
				CompressAndSendPacket(pBuffer, reinterpret_cast<const NetPacket*>(pOverlapped));
			}
			break;
		}

		//
		//	End Thread Completion Function
		//
	})
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

			ReceiveOffset += PacketSize;
			AddressOffset += sizeof(SOCKADDR_INET);

			//printf("Buffer Slice %i\n", (int)i);
			if (i < MaxReceives)
			{
				pBuf->completionKey = CK_RECEIVE;
				if (!g_rio.RIOReceiveEx(RequestQueue, pBuf, 1, NULL, pBuf->pAddrBuff, NULL, NULL, NULL, pBuf))
				{
					printf("RIO Receive %i Failed %i\n", (int)i, WSAGetLastError());
				}
			} else {
				//	This buffer will be used for sends so add it to our queue
				pBuf->completionKey = CK_SEND;
				Data_Buffers.push(pBuf);
			}
		}
		if (g_rio.RIONotify(CompletionQueue) != ERROR_SUCCESS) { printf("\tRIO Notify Failed\n"); return; }

		//	Set Priority
		//GetCurrentThread();
		//SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
		printf("Listening On %s\n", Address->FormattedAddress());
	}

	//	NetSocket Destructor
	//
	NetSocket::~NetSocket()
	{
		printf("\tShutdown Socket\n");
		shutdown(Socket, SD_BOTH);		//	Prohibit Socket from conducting any more Sends or Receives
		this->ShutdownThreads();		//	Shutdown the threads in our Thread Pool
		printf("Close Socket\n");
		closesocket(Socket);			//	Shutdown Socket
										//	Cleanup our Completion Queue
		printf("Close Completion Queue\n");
		g_rio.RIOCloseCompletionQueue(CompletionQueue);
		//	Cleanup our RIO Buffers
		printf("Deregister Buffers\n");
		g_rio.RIODeregisterBuffer(Address_BufferID);
		g_rio.RIODeregisterBuffer(Data_BufferID);
		//	Cleanup other memory
		//delete[] uncompressed_data;
		printf("Delete Overlapped and Buffers\n");
		delete Overlapped;
		while (!Data_Buffers.empty())
		{
			PRIO_BUF_EXT Buff = Data_Buffers.top();
			delete Buff->pAddrBuff;
			delete Buff;
			Data_Buffers.pop();
		}
		delete Address_Buffer;
		delete Data_Buffer;

		printf("Stopped Listening On %s\n", Address->FormattedAddress());
		//	Cleanup our NetAddress
		delete Address;
	}
}