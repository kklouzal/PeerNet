#include "PeerNet.h"
#include "lz4.h"

namespace PeerNet
{
	// Hidden Implementation Namespace
	// Only Visible In This File
	namespace
	{
		std::mutex PeersMutex;
		std::unordered_map<std::string, const std::shared_ptr<NetPeer>> Peers;
	}

	//	Retrieve a NetPeer* from it's formatted address or create a new one
	std::shared_ptr<NetPeer> RetrievePeer(const std::string FormattedAddress, NetSocket* Socket)
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
			auto Peer = std::make_shared<NetPeer>(StrIP, StrPort, Socket);
			Peers.emplace(Peer->GetFormattedAddress(), Peer);
			PeersMutex.unlock();
			return Peer;
		}
	}

	//
	//
	//
	//
	//
	//
	// This is a dedicated thread to receive packets for a single socket
	// Put as much workload as you can into this single function
	void NetSocket::IncomingFunction()
	{
#ifdef _DEBUG_THREADS
		printf("PeerNet NetSocket Incoming Thread %s Starting\n", FormattedAddress.c_str());
#endif
		INT NotifyResult = 0;
		DWORD BytesCompleted = 0;
		ULONG_PTR CompletionKey = 0;
		ULONG NumResults = 0;
		ULONG CurResult = 0;
		INT CompressResult = 0;
		PRIO_BUF_EXT pBuffer = 0;
		while (Initialized)
		{
			 
			NotifyResult = g_rio.RIONotify(g_recv_cQueue);
			if (NotifyResult != ERROR_SUCCESS) { printf("RIO Notify Failed(%i)\n", NotifyResult); }

			if (!GetQueuedCompletionStatus(g_recv_IOCP, &BytesCompleted, &CompletionKey, &recv_overlapped, INFINITE))
			{ if (GetLastError() == ERROR_ABANDONED_WAIT_0) { break; } }

			NumResults = g_rio.RIODequeueCompletion(g_recv_cQueue, g_recv_Results, PendingRecvs);
			if (RIO_CORRUPT_CQ == NumResults) { printf("RIO Corrupt Results - Deleting Socket\n"); delete this; return; }

			//	Actually read the data from each received packet
			for (CurResult = 0; CurResult < NumResults; ++CurResult)
			{
				//	Get the raw packet data into our buffer
				pBuffer = reinterpret_cast<PRIO_BUF_EXT>(g_recv_Results[CurResult].RequestContext);

				//	Try to decompress the received data
				CompressResult = LZ4_decompress_safe(&p_recv_dBuffer[pBuffer->Offset], uncompressed_data, g_recv_Results[CurResult].BytesTransferred, PacketSize);
#ifdef _DEBUG_COMPRESSION
				printf("Decompressed: %i->%i\n", g_recv_Results[CurResult].BytesTransferred, CompressResult);
#endif

				if (CompressResult > 0) {
					
					//	Get which peer sent this data
					const std::string SenderIP(inet_ntoa(((SOCKADDR_INET*)&p_addr_dBuffer[pBuffer->pAddrBuff->Offset])->Ipv4.sin_addr));
					const std::string SenderPort(std::to_string(ntohs(((SOCKADDR_INET*)&p_addr_dBuffer[pBuffer->pAddrBuff->Offset])->Ipv4.sin_port)));
					//	Determine which peer this packet belongs to and immediatly pass it to them for processing.
					RetrievePeer(SenderIP + std::string(":") + SenderPort, this)->ReceivePacket(new NetPacket(std::string(uncompressed_data, CompressResult)));
				}
#ifdef _DEBUG_COMPRESSION
				else { printf("Packet Decompression Failed\n"); }
#endif
				//	Push another read request into the queue
				if (!g_rio.RIOReceiveEx(g_requestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, 0, pBuffer)) { printf("RIO Receive2 Failed\n"); }
			}
		}
#ifdef _DEBUG_THREADS
		printf("PeerNet NetSocket Incoming Thread %s Stopping\n", FormattedAddress.c_str());
#endif
	}


	//	This is a dedicated thread to send packets for a single socket
	//	Put as much workload as you can into this single function
	void NetSocket::OutgoingFunction()
	{
#ifdef _DEBUG_THREADS
		printf("PeerNet NetSocket Outgoing Thread %s Starting\n", FormattedAddress.c_str());
#endif
		std::map<unsigned long, NetPacket*const> q_Unreliable;
		std::map<unsigned long, NetPacket*const> q_Ordered;
		std::map<unsigned long, NetPacket*const> q_Reliable;
		PRIO_BUF_EXT pBuffer = 0;

		// This is our Outgoing Threads main loop
		while (Initialized)
		{
			//	Let this thread sleep until we have outgoing packets to process.
			std::unique_lock<std::mutex> OutgoingLock(OutgoingMutex);
			OutgoingCondition.wait(OutgoingLock, [&]() {
				q_Unreliable.swap(q_OutgoingUnreliable);
				bool DontWait = false;
				if (!q_Ordered.empty() || !q_OutgoingOrdered.empty()) {
					q_Ordered.insert(q_OutgoingOrdered.begin(), q_OutgoingOrdered.end());
					q_OutgoingOrdered.clear();
					DontWait = true;
				}
				if (!q_Reliable.empty() || !q_OutgoingReliable.empty()) {
					q_Reliable.insert(q_OutgoingReliable.begin(), q_OutgoingReliable.end());
					q_OutgoingReliable.clear();
					DontWait = true;
				}
				return !q_Unreliable.empty() || DontWait || !Initialized;
			});
			OutgoingLock.unlock();

			//	Grab a send buffer
			pBuffer = SendBuffs.front();
			SendBuffs.pop_front();

			//
			//	Loop through all current outgoing unreliable packets
			for (auto OutgoingPair : q_Unreliable)
			{
				CompressAndSendPacket(pBuffer, OutgoingPair.second);
				//	Release this ack's memory
				delete OutgoingPair.second;
			}
			//	Return our send buffer
			SendBuffs.push_back(pBuffer);

			/**/

			//	Grab a send buffer
			pBuffer = SendBuffs.front();
			SendBuffs.pop_front();
			//
			// Loop through all the current ordered packets
			for (auto OutgoingPair : q_Ordered)
			{
				//	First time being sent; immediatly send; increment send counter; continue loop;
				if (OutgoingPair.second->SendAttempts == 0) { CompressAndSendPacket(pBuffer, OutgoingPair.second); ++OutgoingPair.second->SendAttempts; continue; }

				auto got = OutgoingPair.second->GetPeer()->q_OrderedAcks.find(OutgoingPair.first);
				//	Matching ACK exists
				if (got != OutgoingPair.second->GetPeer()->q_OrderedAcks.end()) {
					//	We've received an ACK however there are still packets with lower id's left to process; continue the loop;
					if (OutgoingPair.first >= OutgoingPair.second->GetPeer()->NextExpectedOrderedACK) {	continue; }
					//	This packet and ACK can be deleted
					printf("\tOrdered - %i -\t %.3fms\n", OutgoingPair.first, std::chrono::duration<double, std::milli>(got->second->GetCreationTime() - OutgoingPair.second->GetCreationTime()).count());
					//	Cleanup the ACK
					delete got->second;
					OutgoingPair.second->GetPeer()->q_OrderedAcks.erase(got);
					//	Cleanup the packet
					delete OutgoingPair.second;
					//	Erase it from the outgoing containers
					q_Ordered.erase(OutgoingPair.first);
					//	Break the loop since our iterators are now invalid
					break;
				}

				//
				//	No matching ACK

				//	Delete the packet if it's reached its max sends
				if (OutgoingPair.second->NeedsDelete())
				{
					//
					//	ToDo: Handle failed ordered send here
					//

					//	cleanup the packet
					delete OutgoingPair.second;
					//	Erase it from the outgoing containers
					q_Ordered.erase(OutgoingPair.first);
					//	Break the loop since our iterators are now invalid
					break;
				}
				//	Otherwise send it if it needs resent.
				if (OutgoingPair.second->NeedsResend()) { CompressAndSendPacket(pBuffer, OutgoingPair.second); continue; }
			}
			//	Return our send buffer
			SendBuffs.push_back(pBuffer);

			/**/

			//	Grab a send buffer
			pBuffer = SendBuffs.front();
			SendBuffs.pop_front();
			//
			//
			// Loop through all the current reliable packets
			for (auto OutgoingPair : q_Reliable)
			{
				if (OutgoingPair.second->SendAttempts == 0) { CompressAndSendPacket(pBuffer, OutgoingPair.second); ++OutgoingPair.second->SendAttempts; continue; }
				//	Immediatly resend the packet if it needs; continue the loop;
				if (OutgoingPair.second->NeedsResend()) { CompressAndSendPacket(pBuffer, OutgoingPair.second); continue; }
				//	If the NetPeer says this packet is still valid; continue the loop;
				if (OutgoingPair.second->GetPeer()->SendPacket_Reliable(OutgoingPair.second)) { continue; }
				//	Otherwise cleanup the packet
				delete OutgoingPair.second;
				//	Erase it from the outgoing containers
				q_Reliable.erase(OutgoingPair.first);
				//	Break the loop since our iterators are now invalid
				break;
			}
			//	Return our send buffer
			SendBuffs.push_back(pBuffer);

			//	Clear the queue so we only get new packets during the next swap
			q_Unreliable.clear();
		}
#ifdef _DEBUG_THREADS
		printf("PeerNet NetSocket Outgoing Thread %s Stopping\n", FormattedAddress.c_str());
#endif
	}

	void NetSocket::CompressAndSendPacket(PRIO_BUF_EXT pBuffer, const NetPacket* const Packet)
	{
		pBuffer->Length = LZ4_compress_default(Packet->GetData().c_str(), &p_send_dBuffer[pBuffer->Offset], Packet->GetData().size(), PacketSize);
		if (pBuffer->Length > 0) {
			memcpy(&p_addr_dBuffer[pBuffer->pAddrBuff->Offset], Packet->GetPeer()->Result->ai_addr, AddrSize);
			g_rio.RIOSendEx(g_requestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, 0, pBuffer);
			//	Immediatly dequeue this send; probably only need 1 send packet this way
			g_rio.RIONotify(g_send_cQueue);
			GetQueuedCompletionStatus(g_send_IOCP, &pBuffer->numberOfBytes, &pBuffer->completionKey, &send_overlapped, INFINITE);
#ifdef _DEBUG_COMPRESSION
			printf("Compressed: %i->%i\n", Packet->GetData().size(), pBuffer->Length);
#endif
		}
#ifdef _DEBUG_COMPRESSION
		else { printf("Packet Compression Failed - %i\n", pBuffer->Length); }
#endif
		//if (GetLastError() == ERROR_ABANDONED_WAIT_0) { return; } }
		g_rio.RIODequeueCompletion(g_send_cQueue, g_send_Results, PendingSends);
	}

	//	Adds an outgoing packet into the send queue
	void NetSocket::AddOutgoingPacket(NetPacket*const Packet)
	{
		if (Packet->GetType() == PacketType::PN_Ordered)
		{
			std::unique_lock<std::mutex> OutgoingLock(OutgoingMutex);
			q_OutgoingOrdered.insert(std::make_pair(Packet->GetPacketID(), Packet));
			OutgoingLock.unlock();
			OutgoingCondition.notify_one(); // Wake our Outgoing Thread if it's sleeping

		} else if (Packet->GetType() == PacketType::PN_Reliable || Packet->GetType() == PacketType::PN_Discovery) {
			std::unique_lock<std::mutex> OutgoingLock(OutgoingMutex);
			q_OutgoingReliable.insert(std::make_pair(Packet->GetPacketID(), Packet));
			OutgoingLock.unlock();
			OutgoingCondition.notify_one(); // Wake our Outgoing Thread if it's sleeping
		} else {
			std::unique_lock<std::mutex> OutgoingLock(OutgoingMutex);
			q_OutgoingUnreliable.insert(std::make_pair(Packet->GetPacketID(), Packet));
			OutgoingLock.unlock();
			OutgoingCondition.notify_one(); // Wake our Outgoing Thread if it's sleeping
		}
	}

	//	DiscoverPeer - Essentially a Connect function
	std::shared_ptr<NetPeer> NetSocket::DiscoverPeer(const std::string StrIP, const std::string StrPort) {
		return RetrievePeer(StrIP + std::string(":") + StrPort, this);
	}

	//	Retrieves the sockets formatted address
	const std::string NetSocket::GetFormattedAddress() const { return FormattedAddress;	}

	//	Constructor
	NetSocket::NetSocket(const std::string StrIP, const std::string StrPort) :
		PendingRecvs(128), PendingSends(128), PacketSize(1472), AddrSize(sizeof(SOCKADDR_INET)),
		p_addr_dBuffer(new char[AddrSize*(PendingRecvs + PendingSends)]),
		p_recv_dBuffer(new char[PacketSize*PendingRecvs]),
		p_send_dBuffer(new char[PacketSize*PendingSends]),
		g_recv_IOCP(CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0)),
		g_send_IOCP(CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0)),
		recv_overlapped(new OVERLAPPED), send_overlapped(new OVERLAPPED),
		uncompressed_data(new char[1436]),
		q_OutgoingReliable(), q_OutgoingUnreliable(), OutgoingMutex(), Initialized(true),
		OutgoingCondition()
	{
		//	Describe our sockets protocol
		addrinfo Hint;
		addrinfo *Result = NULL;
		ZeroMemory(&Hint, sizeof(Hint));
		Hint.ai_family = AF_INET;
		Hint.ai_socktype = SOCK_DGRAM;
		Hint.ai_protocol = IPPROTO_UDP;
		Hint.ai_flags = AI_PASSIVE;

		//	Resolve the servers addrinfo
		if (getaddrinfo(StrIP.c_str(), StrPort.c_str(), &Hint, &Result) != 0) { printf("GetAddrInfo Failed(%i)\n", WSAGetLastError()); }

		//	Resolve our IP and create the formatted address
		if (Result->ai_family == AF_INET)
		{
			char*const ResolvedIP = new char[16];
			inet_ntop(AF_INET, &(((sockaddr_in*)((sockaddr*)Result->ai_addr))->sin_addr), ResolvedIP, 16);
			FormattedAddress = ResolvedIP + std::string(":") + StrPort;
			delete[] ResolvedIP;
		}
		else {
			//return &(((struct sockaddr_in6*)sa)->sin6_addr);
		}

		//	Create a socket with our addrinfo
		Socket = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL, WSA_FLAG_REGISTERED_IO);
		if (Socket == INVALID_SOCKET) { printf("Socket Failed(%i)\n", WSAGetLastError()); }

		//	Bind our servers socket so we can listen for data
		if (bind(Socket, Result->ai_addr, Result->ai_addrlen) == SOCKET_ERROR) { printf("Bind Failed(%i)\n", WSAGetLastError()); }
		freeaddrinfo(Result);

		//	Initialize RIO on this socket
		GUID functionTableID = WSAID_MULTIPLE_RIO;
		DWORD dwBytes = 0;
		if (WSAIoctl(Socket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
			&functionTableID,
			sizeof(GUID),
			(void**)&g_rio,
			sizeof(g_rio),
			&dwBytes, 0, 0) == SOCKET_ERROR) { printf("Initialize RIO Failed(%i)\n", WSAGetLastError()); }

		//	Create Recv Completion Queue
		RIO_NOTIFICATION_COMPLETION recv_completionType;
		recv_completionType.Type = RIO_IOCP_COMPLETION;
		recv_completionType.Iocp.IocpHandle = g_recv_IOCP;
		recv_completionType.Iocp.CompletionKey = (void*)0;
		recv_completionType.Iocp.Overlapped = recv_overlapped;
		g_recv_cQueue = g_rio.RIOCreateCompletionQueue(PendingRecvs, &recv_completionType);
		if (g_recv_cQueue == RIO_INVALID_CQ) { printf("RIO Recv Completion Queue Failed(%i)\n", WSAGetLastError()); }

		//	Create send Completion Queue
		RIO_NOTIFICATION_COMPLETION send_completionType;
		send_completionType.Type = RIO_IOCP_COMPLETION;
		send_completionType.Iocp.IocpHandle = g_send_IOCP;
		send_completionType.Iocp.CompletionKey = (void*)0;
		send_completionType.Iocp.Overlapped = send_overlapped;
		g_send_cQueue = g_rio.RIOCreateCompletionQueue(PendingSends, &send_completionType);
		if (g_send_cQueue == RIO_INVALID_CQ) { printf("RIO Send Completion Queue Failed(%i)\n", WSAGetLastError()); }

		//	Create Send/Receive Request Queue
		g_requestQueue = g_rio.RIOCreateRequestQueue(Socket, PendingRecvs, 1, PendingSends, 1, g_recv_cQueue, g_send_cQueue, NULL);
		if (g_requestQueue == RIO_INVALID_RQ) { printf("RIO Request Queue Failed(%i)\n", WSAGetLastError()); }

		//	Initialize SOCKADDR_INET Memory Buffer
		DWORD AddrOffset = 0;
		RIO_BUFFERID AddrBufferID = g_rio.RIORegisterBuffer(p_addr_dBuffer, AddrSize*(PendingRecvs + PendingSends));
		if (AddrBufferID == RIO_INVALID_BUFFERID) { printf("RIO Addr Invalid BufferID\n"); }

		//	Initialize Recv Memory Buffer
		DWORD RecvOffset = 0;
		RIO_BUFFERID RecvBufferID = g_rio.RIORegisterBuffer(p_recv_dBuffer, PacketSize*PendingRecvs);
		if (RecvBufferID == RIO_INVALID_BUFFERID) { printf("RIO Recv Invalid BufferID\n"); }

		//	Split buffer into chunks, fill some of our SOCKADDR Buffer, and queue up a receive for each chunk
		for (DWORD i = 0; i < PendingRecvs; ++i)
		{
			PRIO_BUF_EXT pBuf = new RIO_BUF_EXT;
			pBuf->BufferId = RecvBufferID;
			pBuf->Offset = RecvOffset;
			pBuf->Length = PacketSize;
			pBuf->pAddrBuff = new RIO_BUF;
			pBuf->pAddrBuff->BufferId = AddrBufferID;
			pBuf->pAddrBuff->Offset = AddrOffset;
			pBuf->pAddrBuff->Length = AddrSize;
			AddrBuffs.push_back(pBuf);

			RecvOffset += PacketSize;
			AddrOffset += AddrSize;

			if (!g_rio.RIOReceiveEx(g_requestQueue, pBuf, 1, NULL, pBuf->pAddrBuff, NULL, NULL, 0, pBuf))
			{
				printf("RIO Receive Failed %i\n", WSAGetLastError());
			}
		}

		//	Initialize Send Memory Buffer
		DWORD SendOffset = 0;
		RIO_BUFFERID SendBufferID = g_rio.RIORegisterBuffer(p_send_dBuffer, PacketSize*PendingSends);
		if (SendBufferID == RIO_INVALID_BUFFERID) { printf("RIO Send Invalid BufferID\n"); }

		//	Split buffer into chunks and fill the rest of our SOCKADDR Buffer
		for (DWORD i = 0; i < PendingSends; ++i)
		{
			PRIO_BUF_EXT pBuf = new RIO_BUF_EXT;
			pBuf->BufferId = SendBufferID;
			pBuf->Offset = SendOffset;
			pBuf->Length = PacketSize;
			pBuf->pAddrBuff = new RIO_BUF;
			pBuf->pAddrBuff->BufferId = AddrBufferID;
			pBuf->pAddrBuff->Offset = AddrOffset;
			pBuf->pAddrBuff->Length = AddrSize;
			SendBuffs.push_back(pBuf);

			SendOffset += PacketSize;
			AddrOffset += AddrSize;
		}

		//	Create our threads
		OutgoingThread = std::thread(std::thread(&NetSocket::OutgoingFunction, this));
		IncomingThread = std::thread(std::thread(&NetSocket::IncomingFunction, this));
		//	Set Priority
		//GetCurrentThread();
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
		printf("Listening On %s\n", FormattedAddress.c_str());
	}

	//	Destructor
	NetSocket::~NetSocket()
	{
		Initialized = false;			//	Stop Thread Loops
		shutdown(Socket, SD_BOTH);		//	Prohibit Socket from conducting any Sends/Receives
		CloseHandle(g_recv_IOCP);		//	Terminates any current call to GetQueuedCompletionStatus on the specific IOCP Port
		CloseHandle(g_send_IOCP);		//	Terminates any current call to GetQueuedCompletionStatus on the specific IOCP Port
		OutgoingCondition.notify_all();	//	Awaken our Outgoing Thread if it's asleep
		OutgoingThread.join();			//	Block until this thread finishes
		IncomingThread.join();			//	Block until this thread finishes
		closesocket(Socket);			//	Shutdown Socket

		//	Cleanup Our Memory
		delete[] uncompressed_data;
		delete recv_overlapped;
		delete send_overlapped;
		for (auto Buff : SendBuffs)
		{
			delete Buff->pAddrBuff;
			delete Buff;
		}
		for (auto Buff : AddrBuffs)
		{
			delete Buff->pAddrBuff;
			delete Buff;
		}
		delete p_addr_dBuffer;
		delete p_recv_dBuffer;
		delete p_send_dBuffer;

		printf("Stopped Listening On %s\n", FormattedAddress.c_str());
	}
}