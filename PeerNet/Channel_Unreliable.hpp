#pragma once

namespace PeerNet
{
	struct UnreliableOperation
	{
		//	IN
		std::atomic<unsigned long> IN_LastID = 0;	//	The largest received ID so far
		//	OUT
		std::atomic<unsigned long> OUT_NextID = 1;	//	Next packet ID we'll use
	};
	class UnreliableChannel
	{
		NetAddress*const Address;
		const PacketType ChannelID;

		std::mutex IN_Mutex;

		std::unordered_map<unsigned long, UnreliableOperation> Operations;

		std::deque<ReceivePacket*> NeedsProcessed;	//	Packets that need to be processed

	public:
		inline UnreliableChannel(NetAddress*const Addr, const PacketType ChanID)
			: Address(Addr), ChannelID(ChanID),
			IN_Mutex(), NeedsProcessed() {}

		//	Initialize and return a new packet for sending
		inline SendPacket* NewPacket(const unsigned long& OP)
		{
			return new SendPacket(Operations[OP].OUT_NextID++, ChannelID, OP, Address, true);
		}

		//	Swaps the NeedsProcessed queue with an external empty queue (from another thread)
		inline void SwapProcessingQueue(std::deque<ReceivePacket*> &Queue)
		{
			IN_Mutex.lock();
			NeedsProcessed.swap(Queue);
			IN_Mutex.unlock();
		}

		//	Receives a packet
		inline void Receive(ReceivePacket*const IN_Packet)
		{
			if (IN_Packet->GetPacketID() <= Operations[IN_Packet->GetOperationID()].IN_LastID.load()) { delete IN_Packet; return; }
			Operations[IN_Packet->GetOperationID()].IN_LastID.store(IN_Packet->GetPacketID());
			IN_Mutex.lock();
			NeedsProcessed.push_back(IN_Packet);
			IN_Mutex.unlock();
		}
	};
}