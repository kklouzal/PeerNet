#pragma once
#include "ThreadPool.hpp"

namespace PeerNet
{
	struct RIO_BUF_EXT : public RIO_BUF
	{
		NetPeer* Recipient;
		PRIO_BUF pAddrBuff;

		//	Values Filled Upon Call To GetQueuedCompletionStatus
		//	Unused and basically just a way to allocate these variables upfront and only once
		DWORD numberOfBytes = 0;
		ULONG_PTR completionKey = 0;
		//
	}; typedef RIO_BUF_EXT* PRIO_BUF_EXT;

	class NetSocket : public ThreadPoolIOCP
	{
		//	Maximum size of an individual packet in bytes
		const DWORD PacketSize = 1472;
		//	Maximum pending receive packets
		const DWORD MaxReceives = 128;
		const DWORD MaxSends = 128;

		const NetAddress*const Address;
		SOCKET Socket;
		RIO_EXTENSION_FUNCTION_TABLE g_rio;

		// Completion Queue
		RIO_CQ CompletionQueue;
		OVERLAPPED* Overlapped;
		//	CompletionResults MUST ALWAYS be equal to MaxReceives + MaxSends
		RIORESULT CompletionResults[256];
		// Send/Receive Request Queue
		RIO_RQ RequestQueue;
		//	Address Buffer
		RIO_BUFFERID Address_BufferID;
		PCHAR const Address_Buffer;
		//	Data Buffer
		RIO_BUFFERID Data_BufferID;
		PCHAR const Data_Buffer;
		std::deque<PRIO_BUF_EXT> Data_Buffers;

	public:
		void CompressAndSendPacket(PRIO_BUF_EXT pBuffer, const NetPacket * const SendPacket);
		//	NetSocket Constructor
		//
		//	args	-	StrIP, StrPort	-	IP and Port this socket will bind to
		//
		//	Resolve the input address
		//	Create our Socket
		//	Initialize our Base Class (ThreadPoolIOCP) and use a lambda to specify each threads completion function
		NetSocket(const std::string StrIP, const std::string StrPort);
		~NetSocket();

	};

	//	Checks for an existing connected peer and returns it
	//	Or returns a newly constructed NetPeer and immediatly sends the discovery packet
	NetPeer*const RetrievePeer(const std::string FormattedAddress, NetSocket*const Socket);
}