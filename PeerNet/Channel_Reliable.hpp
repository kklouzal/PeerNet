#pragma once

namespace PeerNet
{
	struct ReliableOperation
	{
		//	IN
		std::atomic<unsigned long> IN_LastID = 0;	//	The largest received ID so far
													//	OUT
		std::atomic<unsigned long> OUT_NextID = 1;	//	Next packet ID we'll use
		std::atomic<unsigned long> OUT_LastACK = 0;	//	Next packet ID we'll use
		std::unordered_map<unsigned long, const std::shared_ptr<NetPacket>> OUT_Packets;	//	Unacknowledged outgoing packets
	};
	class ReliableChannel
	{
		const NetAddress*const Address;
		const PacketType ChannelID;

		std::mutex OUT_Mutex;
		std::mutex IN_Mutex;

		std::unordered_map<unsigned long, ReliableOperation> Operations;

		std::queue<ReceivePacket*> NeedsProcessed;	//	Packets that need to be processed

	public:
		inline ReliableChannel(const NetAddress*const Addr, const PacketType &ChanID)
			: Address(Addr), ChannelID(ChanID),
			IN_Mutex(), OUT_Mutex(), NeedsProcessed() {}

		//	Acknowledge all packets up to this ID
		inline void ACK(const unsigned long& ID, const unsigned long& OP)
		{
			//	We hold onto all the sent packets with an ID higher than that of
			//	Which our remote peer has not confirmed delivery for as those
			//	Packets may still be going through their initial sending process
			if (ID > Operations[OP].OUT_LastACK)
			{
				Operations[OP].OUT_LastACK = ID;
#ifdef _PERF_SPINLOCK
				while (!OUT_Mutex.try_lock()) {}
#else
				OUT_Mutex.lock();
#endif
				auto it = Operations[OP].OUT_Packets.begin();
				while (it != Operations[OP].OUT_Packets.end()) {
					if (it->first <= Operations[OP].OUT_LastACK.load()) {
						Operations[OP].OUT_Packets.erase(it++);
						continue;
					}
					++it;
				}
				OUT_Mutex.unlock();
			}
			//	If their last received reliable ID is less than our last sent reliable id
			//	Send the most recently sent reliable packet to them again
			//	Note: if this packet is still in-transit it will be sent again
			//	ToDo:	Hold off on resending the packet until it's creation time
			//			is greater than this clients RTT
			//if (ID < Out_NextID - 1 && Out_Packets.count(Out_NextID - 1)) { MyPeer->Socket->PostCompletion<NetPacket*>(CK_SEND, Out_Packets[Out_NextID - 1].get()); }
		}

		//	Initialize and return a new packet for sending
		inline std::shared_ptr<SendPacket> NewPacket(const unsigned long& OP)
		{
			std::shared_ptr<SendPacket> Packet = std::make_shared<SendPacket>(Operations[OP].OUT_NextID.load(), ChannelID, OP, Address);
#ifdef _PERF_SPINLOCK
			while (!OUT_Mutex.try_lock()) {}
#else
			OUT_Mutex.lock();
#endif
			Operations[OP].OUT_Packets.emplace(Operations[OP].OUT_NextID++, Packet);
			OUT_Mutex.unlock();
			return Packet;
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

		//	Get the largest received ID so far
		//inline const auto GetLastID() const { return IN_LastID.load(); }
	};
}