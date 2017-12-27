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
		const NetAddress*const Address;
		const PacketType ChannelID;

		std::mutex OUT_Mutex;
		std::mutex IN_Mutex;

		std::unordered_map<unsigned long, UnreliableOperation> Operations;

		std::queue<ReceivePacket*> NeedsProcessed;	//	Packets that need to be processed

	public:
		inline UnreliableChannel(const NetAddress*const Addr, const PacketType &ChanID)
			: Address(Addr), ChannelID(ChanID),
			IN_Mutex(), OUT_Mutex(), NeedsProcessed() {}

		//	Initialize and return a new packet for sending
		inline SendPacket*const NewPacket(const unsigned long& OP)
		{
			return new SendPacket(Operations[OP].OUT_NextID++, ChannelID, OP, Address, true);
		}

		//	Swaps the NeedsProcessed queue with an external empty queue (from another thread)
		inline void SwapProcessingQueue(std::queue<ReceivePacket*> &Queue)
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
			NeedsProcessed.push(IN_Packet);
			IN_Mutex.unlock();
		}
	};
}