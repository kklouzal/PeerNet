#pragma once

namespace PeerNet
{
	struct OrderedOperation
	{
		//	IN
		unsigned long IN_LowestID = 0;	//	The lowest received ID
		unsigned long IN_HighestID = 0;	//	Highest received ID
		std::unordered_map<unsigned long, ReceivePacket*> IN_StoredIDs;	//	Incoming packets we cant process yet
		//	OUT
		std::atomic<unsigned long> OUT_NextID = 1;	//	The next packet ID we'll use
		std::unordered_map<unsigned long, SendPacket*> OUT_Packets;	//	Unacknowledged outgoing packets
	};

	class OrderedChannel
	{
		NetAddress*const Address;
		const PacketType ChannelID;

		std::mutex IN_Mutex;
		std::mutex OUT_Mutex;
		std::deque<SendPacket*> OUT_ACKs;	//	ACKs that need to be deleted

		std::unordered_map<unsigned long, OrderedOperation> Operations;

		std::deque<ReceivePacket*> NeedsProcessed;	//	Packets that need to be processed

	public:
		//	Default constructor initializes us and our base class
		inline OrderedChannel(NetAddress*const Addr, const PacketType &ChanID)
			: Address(Addr), ChannelID(ChanID),
			IN_Mutex(), OUT_Mutex(), NeedsProcessed() {}

		//	Acknowledge delivery of a single packet
		//	TODO:
		//	This will cause a resource leak when IsSending.load == false
		//	Since it wont get erased from OUT_Packets, it will eventually resend
		//	and eventually we'll receive an ack which deletes it
		inline void ACK(const unsigned long& ID, const unsigned long& OP)
		{
			//	Grab our OrderedOperation; Creates a new one if not exists
			auto it = Operations[OP].OUT_Packets.find(ID);
			if (it != Operations[OP].OUT_Packets.end()) {
				it->second->NeedsDelete.store(1);
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
			SendPacket* ACK = new SendPacket(IncomingPacket->GetPacketID(), PN_Ordered, IncomingPacket->GetOperationID(), Address, true, IncomingPacket->GetCreationTime());
			ACK->WriteData<bool>(true);	//	Is an ACK
			OUT_Mutex.lock();
			OUT_ACKs.push_back(ACK);
			OUT_Mutex.unlock();
			return ACK;
		}

		//	Resends all unacknowledged packets across a specific NetSocket
		inline void ResendUnacknowledged(NetSocket*const Socket)
		{
			OUT_Mutex.lock();
			//	Cleanup sent ACKs
			auto Packet = OUT_ACKs.begin();
			while (Packet != OUT_ACKs.end())
			{
				if ((*Packet)->NeedsDelete == 1) {
					delete (*Packet);
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

		//	Receives an ordered packet
		inline void Receive(ReceivePacket*const IN_Packet)
		{
			//	Process a data packet
#ifdef _PERF_SPINLOCK
			while (!IN_Mutex.try_lock()) {}
#else
			IN_Mutex.lock();
#endif
			//	Cache our OrderedOperation
			OrderedOperation* OP = &Operations[IN_Packet->GetOperationID()];

			//	Ignore ID's below the LowestID
			if (IN_Packet->GetPacketID() <= OP->IN_LowestID)
			{ IN_Mutex.unlock(); delete IN_Packet; return; }

			//	Ignore ID's we've already stored
			if (OP->IN_StoredIDs.count(IN_Packet->GetPacketID()))
			{
				IN_Mutex.unlock(); delete IN_Packet; return;
			}

			//	Update our HighestID if needed
			if (IN_Packet->GetPacketID() > OP->IN_HighestID)
			{ OP->IN_HighestID = IN_Packet->GetPacketID(); }


			//	(in-sequence processing)
			//	Update our LowestID if needed
			if (IN_Packet->GetPacketID() == OP->IN_LowestID + 1) {
				++OP->IN_LowestID;
				//printf("Ordered - %d - %s\tNew\n", IN_Packet->GetPacketID(), IN_Packet->ReadData<std::string>().c_str());
				//	Push this packet into the NeedsProcessed Queue
				NeedsProcessed.push_back(IN_Packet);
				// Loop through our StoredIDs container until we cant find (LowestID+1)
				while (OP->IN_StoredIDs.count(OP->IN_LowestID + 1))
				{
					++OP->IN_LowestID;
					//printf("Ordered - %d - %s\tStored\n", IN_LowestID, IN_StoredIDs.at(IN_LowestID)->ReadData<std::string>().c_str());
					//	Push this packet into the NeedsProcessed Queue
					ReceivePacket* Pkt = OP->IN_StoredIDs.at(OP->IN_LowestID);
					NeedsProcessed.push_back(Pkt);
					//	Erase the ID from our out-of-order map
					OP->IN_StoredIDs.erase(OP->IN_LowestID);
				}
				IN_Mutex.unlock();
				return;
			}


			//	(out-of-sequence processing)
			//	At this point ID must be greater than LowestID
			//	Which means we have an out-of-sequence ID
			OP->IN_StoredIDs.emplace(IN_Packet->GetPacketID(), IN_Packet);
			IN_Mutex.unlock();
		}

		inline void PrintStats()
		{
			auto Operation = Operations.begin();
			while (Operation != Operations.end())
			{
				printf("Ordered Channel (%i) OUT_Packets Size: %zi\n", Operation->first, Operation->second.OUT_Packets.size());
				printf("Ordered Channel (%i) IN_StoredIDs Size: %zi\n", Operation->first, Operation->second.IN_StoredIDs.size());
				++Operation;
			}

		}
	};
}