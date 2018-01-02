#pragma once

namespace PeerNet
{
	struct ReliableOperation
	{
		//	IN
		std::atomic<unsigned long> IN_LastID = 0;	//	The largest received (and acknowledged) ID so far
		//	OUT
		std::atomic<unsigned long> OUT_NextID = 1;	//	Next packet ID we'll use
		std::atomic<unsigned long> OUT_LastACK = 0;	//	Next packet ID we'll use
		std::unordered_map<unsigned long, SendPacket*> OUT_Packets;	//	Unacknowledged outgoing packets
	};
	class ReliableChannel
	{
		NetAddress*const Address;
		const PacketType ChannelID;

		std::mutex IN_Mutex;
		std::mutex OUT_Mutex;
		std::deque<SendPacket*> OUT_ACKs;	//	ACKs that need to be deleted

		std::unordered_map<unsigned long, ReliableOperation> Operations;

		std::deque<ReceivePacket*> NeedsProcessed;	//	Packets that need to be processed

	public:
		inline ReliableChannel(NetAddress*const Addr, const PacketType &ChanID)
			: Address(Addr), ChannelID(ChanID),
			IN_Mutex(), OUT_Mutex(), NeedsProcessed() {}

		//	Acknowledge all packets up to this ID
		inline void ACK(const unsigned long& ID, const unsigned long& OP)
		{
			if (ID <= Operations[OP].OUT_LastACK.load()) { return; }

			Operations[OP].OUT_LastACK.store(ID);
			auto it = Operations[OP].OUT_Packets.begin();
			while (it != Operations[OP].OUT_Packets.end()) {
				if (it->first <= ID) {
					it->second->NeedsDelete.store(1);
				}
				++it;
			}
		}

		//	Initialize and return a new packet for sending
		inline SendPacket* NewPacket(const unsigned long& OP)
		{
			const unsigned long PacketID = Operations[OP].OUT_NextID++;
			SendPacket* Packet = new SendPacket(PacketID, ChannelID, OP, Address);
			Packet->WriteData<bool>(false);	//	Not an ACK
#ifdef _PERF_SPINLOCK
			while (!OUT_Mutex.try_lock()) {}
#else
			OUT_Mutex.lock();
#endif
			Operations[OP].OUT_Packets.emplace(PacketID, Packet);
			OUT_Mutex.unlock();
			return Packet;
		}

		inline SendPacket*const NewACK(ReceivePacket* IncomingPacket, NetAddress* Address)
		{
			SendPacket* ACK = new SendPacket(IncomingPacket->GetPacketID(), PN_Reliable, IncomingPacket->GetOperationID(), Address, true, IncomingPacket->GetCreationTime());
			ACK->WriteData<bool>(true);	//	Is an ACK
			OUT_Mutex.lock();
			OUT_ACKs.push_back(ACK);
			OUT_Mutex.unlock();
			return ACK;
		}

		//	Resends all unacknowledged packets across a specific NetSocket
		inline void ResendUnacknowledged(NetSocket* Socket)
		{
			OUT_Mutex.lock();
			//	Cleanup sent ACKs
			auto Packet = OUT_ACKs.begin();
			while (Packet != OUT_ACKs.end())
			{
				if ((*Packet)->NeedsDelete == 1) {
					delete (*Packet);
					(*Packet) = NULL;
					Packet = OUT_ACKs.erase(Packet);
				}
				else {
					++Packet;
				}
			}
			//	Resend unacknowledged packets
			auto Operation = Operations.begin();
			while (Operation != Operations.end())
			{
				auto Packet = Operation->second.OUT_Packets.begin();
				while (Packet != Operation->second.OUT_Packets.end())
				{
					//	If we're not currently sending
					if (Packet->second->IsSending.load() == 0)
					{
						//	If this packet needs deleted
						if (Packet->second->NeedsDelete.load() == 1)
						{
							delete Packet->second;
							Packet = Operation->second.OUT_Packets.erase(Packet);
							continue;
						}
						//	If this packet doesn't need deleted
						else {
							//	Flag this packet as sending
							Packet->second->IsSending.store(1);
							//	Resend the packet
							Socket->SendPacket(Packet->second);
						}
					}
					//	Move to the next packet
					++Packet;
				}
				//	Move to the next operation
				++Operation;
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
			IN_Mutex.lock();
			Operations[IN_Packet->GetOperationID()].IN_LastID.store(IN_Packet->GetPacketID());
			NeedsProcessed.push_back(IN_Packet);
			IN_Mutex.unlock();
		}

		inline void PrintStats()
		{
			auto Operation = Operations.begin();
			while (Operation != Operations.end())
			{
				printf("Reliable Channel (%i) OUT_Packets Size: %zi\n", Operation->first, Operation->second.OUT_Packets.size());
				++Operation;
			}

		}

		//	Get the largest received ID so far
		//inline const auto GetLastID() const { return IN_LastID.load(); }
	};
}