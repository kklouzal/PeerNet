#pragma once

namespace PeerNet
{
	class OrderedChannel
	{
		const NetAddress*const Address;
		const PacketType ChannelID;

		std::mutex IN_Mutex;
		unsigned long IN_LowestID;	//	The lowest received ID
		unsigned long IN_HighestID;	//	Highest received ID
		//	TODO: Use RAW Pointers
		std::unordered_map<unsigned long, std::shared_ptr<ReceivePacket>> IN_StoredIDs;	//	Incoming packets we cant process yet
		std::unordered_map<unsigned long, bool> IN_MissingIDs;						//	Missing IDs from the ordered sequence

		std::mutex OUT_Mutex;
		std::atomic<unsigned long> OUT_NextID;	//	The next packet ID we'll use
		std::unordered_map<unsigned long, const std::shared_ptr<NetPacket>> OUT_Packets;	//	Unacknowledged outgoing packets

		//	TODO: Use RAW Pointers
		std::queue<std::shared_ptr<ReceivePacket>> NeedsProcessed;	//	Packets that need to be processed

	public:
		//	Default constructor initializes us and our base class
		inline OrderedChannel(const NetAddress*const Addr, const PacketType &ChanID)
			: Address(Addr), ChannelID(ChanID),
			IN_Mutex(), IN_LowestID(0), IN_HighestID(0), IN_StoredIDs(), IN_MissingIDs(), NeedsProcessed(),
			OUT_Mutex(), OUT_NextID(1) {}

		//	Acknowledge delivery of a single packet
		//	TODO:
		//	This will cause a resource leak when IsSending.load == false
		//	Since it wont get erased from OUT_Packets, it will eventually resend
		//	and eventually we'll receive an ack which deletes it
		inline void ACK(const unsigned long& ID)
		{
			auto it = OUT_Packets.find(ID);
			if (it != OUT_Packets.end()) {
				//if (it->second->IsSending.load() == false)
				//{
#ifdef _PERF_SPINLOCK
				while (!OUT_Mutex.try_lock()) {}
#else
				OUT_Mutex.lock();
#endif
				OUT_Packets.erase(it);
				OUT_Mutex.unlock();
				//}
			}
		}

		//	Initialize and return a new packet for sending
		inline std::shared_ptr<SendPacket> NewPacket()
		{
			const unsigned long PacketID = OUT_NextID++;
			std::shared_ptr<SendPacket> Packet = std::make_shared<SendPacket>(PacketID, ChannelID, Address);
			Packet->WriteData<bool>(false);	//	This is not an ACK
#ifdef _PERF_SPINLOCK
			while (!OUT_Mutex.try_lock()) {}
#else
			OUT_Mutex.lock();
#endif
			OUT_Packets.emplace(PacketID, Packet);
			OUT_Mutex.unlock();
			return Packet;
		}

		//	Swaps the NeedsProcessed queue with an external empty queue (from another thread)
		inline void SwapProcessingQueue(std::queue<std::shared_ptr<ReceivePacket>> &Queue)
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
			if (IN_MissingIDs.count(IN_Packet->GetPacketID())) { IN_MissingIDs.erase(IN_Packet->GetPacketID()); }

			//	Ignore ID's below the LowestID
			if (IN_Packet->GetPacketID() <= IN_LowestID) { IN_Mutex.unlock(); return; }

			//	Update our HighestID if needed
			if (IN_Packet->GetPacketID() > IN_HighestID) { IN_HighestID = IN_Packet->GetPacketID(); }

			//	(in-sequence processing)
			//	Update our LowestID if needed
			if (IN_Packet->GetPacketID() == IN_LowestID + 1) {
				++IN_LowestID;
				//printf("Ordered - %d - %s\tNew\n", IN_Packet->GetPacketID(), IN_Packet->ReadData<std::string>().c_str());
				//	Emplace this packet into the NeedsProcessed Queue
				NeedsProcessed.emplace(IN_Packet);
				// Loop through our StoredIDs container until we cant find (LowestID+1)
				while (IN_StoredIDs.count(IN_LowestID + 1))
				{
					++IN_LowestID;
					//printf("Ordered - %d - %s\tStored\n", IN_LowestID, IN_StoredIDs.at(IN_LowestID)->ReadData<std::string>().c_str());
					//	Push this packet into the NeedsProcessed Queue
					NeedsProcessed.push(IN_StoredIDs.at(IN_LowestID));
					//	Erase the ID from our out-of-order map
					IN_StoredIDs.erase(IN_LowestID);
				}
				IN_Mutex.unlock();
				return;
			}

			//	(out-of-sequence processing)
			//	At this point ID must be greater than LowestID
			//	Which means we have an out-of-sequence ID
			IN_StoredIDs.emplace(IN_Packet->GetPacketID(), IN_Packet);
			for (unsigned long i = IN_Packet->GetPacketID() - 1; i > IN_LowestID; --i)
			{
				if (!IN_StoredIDs.count(i)) { IN_MissingIDs[i] = true; }
			}
			IN_Mutex.unlock();
		}
	};
}