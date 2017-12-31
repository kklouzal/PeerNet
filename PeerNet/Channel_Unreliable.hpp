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
		std::mutex OUT_Mutex;
		std::deque<SendPacket*> OUT_Packets;	//	Packets that need to be deleted

		std::unordered_map<unsigned long, UnreliableOperation> Operations;

		std::deque<ReceivePacket*> NeedsProcessed;	//	Packets that need to be processed

	public:
		inline UnreliableChannel(NetAddress*const Addr, const PacketType ChanID)
			: Address(Addr), ChannelID(ChanID),
			IN_Mutex(), NeedsProcessed() {}

		//	Initialize and return a new packet for sending
		inline SendPacket* NewPacket(const unsigned long& OP)
		{
			SendPacket* Packet = new SendPacket(Operations[OP].OUT_NextID++, ChannelID, OP, Address, true);
			OUT_Mutex.lock();
			OUT_Packets.push_back(Packet);
			OUT_Mutex.unlock();
			return Packet;
		}

		inline void DeleteUsed()
		{
			OUT_Mutex.lock();
			auto Packet = OUT_Packets.begin();
			while (Packet != OUT_Packets.end())
			{
				if ((*Packet)->NeedsDelete == 1) {
					delete (*Packet);
					Packet = OUT_Packets.erase(Packet);
				}
				else {
					++Packet;
				}
			}
			OUT_Mutex.unlock();
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