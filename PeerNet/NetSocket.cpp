#include "PeerNet.h"
#include "lz4.h"

namespace PeerNet
{
	// This is a dedicated thread to receive packets for a single socket
	// Put as much workload as you can into this single function
	void NetSocket::IncomingFunction()
	{
		printf("PeerNet NetSocket Incoming Thread %s Starting\n", FormattedAddress.c_str());
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
				pBuffer = reinterpret_cast<PRIO_BUF_EXT>(g_recv_Results[CurResult].RequestContext);

				//	Try to decompress the received data
				CompressResult = LZ4_decompress_safe(&p_recv_dBuffer[pBuffer->Offset], uncompressed_data, g_recv_Results[CurResult].BytesTransferred, PacketSize);
				//printf("Decompressed: %i->%i\n", g_recv_Results[i].BytesTransferred, retVal);

				if (CompressResult > 0) {

					//	Construct a NetPacket from the data
					NetPacket *NewPacket(new NetPacket(std::string(uncompressed_data, CompressResult)));

					//	Get which peer sent this data
					const std::string SenderIP(inet_ntoa(((SOCKADDR_INET*)&p_addr_dBuffer[pBuffer->pAddrBuff->Offset])->Ipv4.sin_addr));
					const std::string SenderPort(std::to_string(ntohs(((SOCKADDR_INET*)&p_addr_dBuffer[pBuffer->pAddrBuff->Offset])->Ipv4.sin_port)));
					auto ThisPeer = GetPeer(SenderIP + ":" + SenderPort);

					switch (NewPacket->GetType())
					{

						//	Acknowledgements are passed to the NetPeer for further handling
						case PacketType::PN_ACK:
							if (ThisPeer != nullptr)
							{
								printf("Recv ACK #%u\n", NewPacket->GetPacketID());
								//	Set the LastSuccessfulACK of this NetPacket's Source NetPeer to the ID of this ACK
								ThisPeer->ProcessACK(NewPacket);
							}
							else {
								printf("Recv ACK Undiscovered Sender");
							}
							break;

						case PacketType::PN_Ordered:
							if (ThisPeer != nullptr)
							{

							}
							else {
								printf("Recv Ordered Undiscovered Sender");
							}
							break;

						//	Reliable packets immediatly send off an acknowledgement then passed to the NetPeer for further handling
						//	The peer will peridocially loop through all the packets they need processed
						//	Checking to see if the packet needs to be processed in an ordered fashion
						//	Or if we need only look at the most recent for that type we've received
						//
						//	ToDo: Have the socket split these two types into different queues
						//		And handle the heavy lifting during this case
						case PacketType::PN_Reliable:
							if (ThisPeer != nullptr)
							{
								//	Get the LastReceivedReliable of this NetPacket's Source NetPeer
								//	If this NetPacket's ID is less or equal to the LastReceivedReliable then disguard the NetPacket
								if (NewPacket->GetPacketID() <= ThisPeer->LastReceivedReliable) { break; }
								ThisPeer->LastReceivedReliable = NewPacket->GetPacketID();
								printf("Recv Reliable\n");
								//ThisPeer->AddPacket(NewPacket);
								// Send ACK
								AddOutgoingPacket(ThisPeer.get(), new NetPacket(NewPacket->GetPacketID(), PacketType::PN_ACK, this, ThisPeer.get()));
							}
							else {
								printf("Recv Reliable Undiscovered Sender");
							}
							break;

						//	Unreliable packets are given to the NetPeer for further handling
						case PacketType::PN_Unreliable:
							if (ThisPeer != nullptr)
							{
								printf("Recv Unreliable\n");
								//ThisPeer->AddPacket(NewPacket);
							}
							else {
								printf("Recv Undiscovered Sender");
							}
							break;

					//	Client Discovery Protocol:
					//	Send Discovery Packet to Host
					//	Host sends Discovery Packet back to Peer
					//	Peer sets self acknowledged
					case PacketType::PN_Discovery:
						if (ThisPeer == nullptr)
						{
							//	We're receiving a request for the first time.

							//	Create a new NetPeer
							//	ToDo: NetPeers need a way of announcing themselves after initial creation
							DiscoverPeer(SenderIP.c_str(), SenderPort.c_str());
						}
						else {
							//	We're receiving an acknowledgement of a request we created
							ThisPeer->SetAcknowledged();
							// Send ACK
							AddOutgoingPacket(ThisPeer.get(), new NetPacket(NewPacket->GetPacketID(), PacketType::PN_ACK, this, ThisPeer.get()));
							//	Acknowledge this packet
							ThisPeer->ProcessACK(NewPacket);
						}
						break;

					default:
						printf("Received Unknown Packet Type\n");
					}

					delete NewPacket;
				}
				else { printf("Packet Decompression Failed\n"); }
				//	Push another read request into the queue
				if (!g_rio.RIOReceiveEx(g_requestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, 0, pBuffer)) { printf("RIO Receive2 Failed\n"); }
			}
		}
		printf("PeerNet NetSocket Incoming Thread %s Stopping\n", FormattedAddress.c_str());
	}


	//	This is a dedicated thread to send packets for a single socket
	//	Put as much workload as you can into this single function
	void NetSocket::OutgoingFunction()
	{
		printf("PeerNet NetSocket Outgoing Thread %s Starting\n", FormattedAddress.c_str());
		std::queue<NetPacket*> q_OPackets;
		PRIO_BUF_EXT pBuffer = 0;

		// This is our Outgoing Threads main loop
		while (Initialized)
		{
			//	Let this thread sleep until we have outgoing packets to process.
			//	Swap all the current outgoing packets into a new unordered_map so we can quickly iterate over them
			//	Any reliable packets left over from the last loop will be swapped back into the main unordered_map
			std::unique_lock<std::mutex> OutgoingLock(OutgoingMutex);
			OutgoingCondition.wait(OutgoingLock, [&]() { return (!q_OPackets.empty() || !q_OutgoingPackets.empty() || !Initialized); });
			q_OutgoingPackets.swap(q_OPackets);
			OutgoingLock.unlock();

			pBuffer = SendBuffs.front();
			SendBuffs.pop_front();
			// Loop through all the current outgoing packets
			while (!q_OPackets.empty())
			{
				//	Check and see if this is a reliable packet
				if (q_OPackets.front()->IsReliable())
				{
					//	Check if we've received an ACK for this packet
					//	If this NetPacket's ID is less or equal to the LastSuccessfulACK we sent to this NetPacket's NetPeer
					//	Destroy the packet since we have acknowledgement of it's delivery
					if (q_OPackets.front()->GetPacketID() <= q_OPackets.front()->GetPeer()->GetLastAck())
					{
						if (q_OPackets.front()->GetPacketID() == q_OPackets.front()->GetPeer()->GetLastAck())
						{
							q_OPackets.front()->Acknowledge(q_OPackets.front()->GetPeer()->GetLastAckTime());
						}
						delete q_OPackets.front();
						q_OPackets.pop();
						continue;//
					}
					else
					{
						//	We haven't got an acknowledgement for this reliable packet yet,
						//	See if we can try to send it again.
						//Pair.second->LastReceivedReliablePacketID = Pair.first->GetPacketID();
						if (q_OPackets.front()->SendAttempts == 0)
						{
							q_OPackets.front()->SendAttempts++;
						}
						else if (q_OPackets.front()->SendAttempts < 5)
						{
							//	If it's not time for us to send again then jump to the next packet
							if (!q_OPackets.front()->NeedsResend()) { continue; }
							q_OPackets.front()->SendAttempts++;
						}
						else
						{
							if (q_OPackets.front()->GetType() == PacketType::PN_Discovery)
							{
								//	Failed discovery, cleanup peer
								//Pair.second
							}
							delete q_OPackets.front();
							q_OPackets.pop();
							continue;//
						}
					}
				}
				pBuffer->Length = LZ4_compress_default(q_OPackets.front()->GetData().c_str(), &p_send_dBuffer[pBuffer->Offset], q_OPackets.front()->GetData().size(), PacketSize);

				if (pBuffer->Length > 0)
				{
					memcpy(&p_addr_dBuffer[pBuffer->pAddrBuff->Offset], q_OPackets.front()->GetPeer()->Result->ai_addr, AddrSize);
					g_rio.RIOSendEx(g_requestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, 0, pBuffer);
					//	Immediatly dequeue this send; probably only need 1 send packet this way
					g_rio.RIONotify(g_send_cQueue);
					GetQueuedCompletionStatus(g_send_IOCP, &pBuffer->numberOfBytes, &pBuffer->completionKey, &send_overlapped, INFINITE);
					//if (GetLastError() == ERROR_ABANDONED_WAIT_0) { return; } }
					g_rio.RIODequeueCompletion(g_send_cQueue, g_send_Results, PendingSends);
				}
				else { printf("Failed Compression!\n"); }

				// If this packet wasnt reliable, release the packets memory and remove it from the unordered_map
				if (!q_OPackets.front()->IsReliable())
				{
					delete q_OPackets.front();
					q_OPackets.pop();
					continue;//
				}
			}
			SendBuffs.push_back(pBuffer);
		}
		printf("PeerNet NetSocket Outgoing Thread %s Stopping\n", FormattedAddress.c_str());
	}

	//	DiscoverPeer - Essentially a Connect function
	std::shared_ptr<NetPeer> NetSocket::DiscoverPeer(const std::string StrIP, const std::string StrPort)
	{
		auto Peer = GetPeer(FormattedAddress);
		if (Peer == nullptr)
		{
			//	Create a new NetPeer into the Peers variable 
			Peer = std::make_shared<NetPeer>(StrIP, StrPort, this);
			AddPeer(Peer->GetFormattedAddress(), Peer);
			//	Send a discovery request to this newly created NetPeer
			AddOutgoingPacket(Peer.get(), Peer.get()->CreateNewPacket(PacketType::PN_Discovery));
			//	Finally return our newly created NetPeer
		}
		return Peer;
	}

	//	Adds an outgoing packet into the send queue
	void NetSocket::AddOutgoingPacket(NetPeer*const Peer, NetPacket*const Packet)
	{
		std::unique_lock<std::mutex> OutgoingLock(OutgoingMutex);
		q_OutgoingPackets.push(Packet);
		OutgoingLock.unlock();
		OutgoingCondition.notify_one(); // Wake our Outgoing Thread if it's sleeping
	}

	//	Retrieves the sockets formatted address
	const std::string NetSocket::GetFormattedAddress() const
	{
		return FormattedAddress;
	}

	//	Constructor
	NetSocket::NetSocket(const std::string StrIP, const std::string StrPort) :
		PendingRecvs(1024), PendingSends(128), PacketSize(1472), AddrSize(sizeof(SOCKADDR_INET)),
		p_addr_dBuffer(new char[AddrSize*(PendingRecvs + PendingSends)]),
		p_recv_dBuffer(new char[PacketSize*PendingRecvs]),
		p_send_dBuffer(new char[PacketSize*PendingSends]),
		g_recv_IOCP(CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0)),
		g_send_IOCP(CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0)),
		recv_overlapped(new OVERLAPPED), send_overlapped(new OVERLAPPED),
		uncompressed_data(new char[1436]),
		q_OutgoingPackets(), OutgoingMutex(), Initialized(true),
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
		/*Peers.reserve(128);*/ printf("PeerNet NetSocket %s Created\n", FormattedAddress.c_str());
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

		printf("PeerNet NetSocket %s Destroyed\n", FormattedAddress.c_str());
	}
}