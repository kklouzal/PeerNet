#pragma once

namespace PeerNet
{
	struct OrderedOperation
	{
		//	IN
		unsigned long IN_LowestID = 0;	//	The lowest received ID
		unsigned long IN_HighestID = 0;	//	Highest received ID
		std::unordered_map<unsigned long, ReceivePacket*> IN_StoredIDs;	//	Incoming packets we cant process yet
		std::unordered_map<unsigned long, bool> IN_MissingIDs;						//	Missing IDs from the ordered sequence
		//	OUT
		std::atomic<unsigned long> OUT_NextID = 1;	//	The next packet ID we'll use
		std::unordered_map<unsigned long, SendPacket*> OUT_Packets;	//	Unacknowledged outgoing packets
	};

	class OrderedChannel
	{
		const NetAddress*const Address;
		const PacketType ChannelID;

		std::mutex IN_Mutex;
		std::mutex OUT_Mutex;

		std::unordered_map<unsigned long, OrderedOperation> Operations;

		std::queue<ReceivePacket*> NeedsProcessed;	//	Packets that need to be processed

	public:
		//	Default constructor initializes us and our base class
		inline OrderedChannel(const NetAddress*const Addr, const PacketType &ChanID)
			: Address(Addr), ChannelID(ChanID),
			IN_Mutex(), OUT_Mutex(), NeedsProcessed() {}

		//	Acknowledge delivery of a single packet
		//	TODO:
		//	This will cause a resource leak when IsSending.load == false
		//	Since it wont get erased from OUT_Packets, it will eventually resend
		//	and eventually we'll receive an ack which deletes it
		inline void ACK(const unsigned long& ID, const unsigned long& OP)
		{
			//if (ID <= Operations[OP].IN_LowestID.load()) { return; }
#ifdef _PERF_SPINLOCK
			while (!OUT_Mutex.try_lock()) {}
#else
			OUT_Mutex.lock();
#endif
			//	Grab our OrderedOperation; Creates a new one if not exists
			auto it = Operations[OP].OUT_Packets.find(ID);
			if (it != Operations[OP].OUT_Packets.end()) {
				it->second->NeedsDelete.store(1);
			}
			OUT_Mutex.unlock();
		}

		//	Initialize and return a new packet for sending
		inline SendPacket*const NewPacket(const unsigned long& OP)
		{
			const unsigned long PacketID = Operations[OP].OUT_NextID++;
			SendPacket*const Packet = new SendPacket(PacketID, ChannelID, OP, Address);
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

		//	Resends all unacknowledged packets across a specific NetSocket
		inline void ResendUnacknowledged(NetSocket*const Socket)
		{
			OUT_Mutex.lock();
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
							Socket->PostCompletion<SendPacket*const>(CK_SEND, Packet->second);
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
		inline void SwapProcessingQueue(std::queue<ReceivePacket*> &Queue)
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
			//	If this ID was missing, remove it from the MissingIDs container
			if (Operations[IN_Packet->GetOperationID()].IN_MissingIDs.count(IN_Packet->GetPacketID()))
			{ Operations[IN_Packet->GetOperationID()].IN_MissingIDs.erase(IN_Packet->GetPacketID()); }

			//	Ignore ID's below the LowestID
			if (IN_Packet->GetPacketID() <= Operations[IN_Packet->GetOperationID()].IN_LowestID)
			{ IN_Mutex.unlock(); delete IN_Packet; return; }

			//	Update our HighestID if needed
			if (IN_Packet->GetPacketID() > Operations[IN_Packet->GetOperationID()].IN_HighestID)
			{ Operations[IN_Packet->GetOperationID()].IN_HighestID = IN_Packet->GetPacketID(); }

			//	(in-sequence processing)
			//	Update our LowestID if needed
			if (IN_Packet->GetPacketID() == Operations[IN_Packet->GetOperationID()].IN_LowestID + 1) {
				++Operations[IN_Packet->GetOperationID()].IN_LowestID;
				//printf("Ordered - %d - %s\tNew\n", IN_Packet->GetPacketID(), IN_Packet->ReadData<std::string>().c_str());
				//	Push this packet into the NeedsProcessed Queue
				NeedsProcessed.push(IN_Packet);
				// Loop through our StoredIDs container until we cant find (LowestID+1)
				while (Operations[IN_Packet->GetOperationID()].IN_StoredIDs.count(Operations[IN_Packet->GetOperationID()].IN_LowestID + 1))
				{
					++Operations[IN_Packet->GetOperationID()].IN_LowestID;
					//printf("Ordered - %d - %s\tStored\n", IN_LowestID, IN_StoredIDs.at(IN_LowestID)->ReadData<std::string>().c_str());
					//	Push this packet into the NeedsProcessed Queue
					NeedsProcessed.push(Operations[IN_Packet->GetOperationID()].IN_StoredIDs.at(Operations[IN_Packet->GetOperationID()].IN_LowestID));
					//	Erase the ID from our out-of-order map
					Operations[IN_Packet->GetOperationID()].IN_StoredIDs.erase(Operations[IN_Packet->GetOperationID()].IN_LowestID);
				}
				IN_Mutex.unlock();
				return;
			}

			//	(out-of-sequence processing)
			//	At this point ID must be greater than LowestID
			//	Which means we have an out-of-sequence ID
			Operations[IN_Packet->GetOperationID()].IN_StoredIDs.emplace(IN_Packet->GetPacketID(), IN_Packet);
			for (unsigned long i = IN_Packet->GetPacketID() - 1; i > Operations[IN_Packet->GetOperationID()].IN_LowestID; --i)
			{
				if (!Operations[IN_Packet->GetOperationID()].IN_StoredIDs.count(i))
				{ Operations[IN_Packet->GetOperationID()].IN_MissingIDs[i] = true; }
			}
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