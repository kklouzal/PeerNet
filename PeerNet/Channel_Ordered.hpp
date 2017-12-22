#pragma once

namespace PeerNet
{
	class OrderedChannel : public Channel
	{
		std::unordered_map<unsigned long, shared_ptr<ReceivePacket>> IN_StoredIDs;	//	Incoming packets we cant process yet
		std::unordered_map<unsigned long, bool> IN_MissingIDs;	//	Missing IDs from the ordered sequence

		unsigned int IN_HighestID;	//	Highest received ID

	public:
		//	Default constructor initializes us and our base class
		OrderedChannel(const NetAddress*const Address, const PacketType &ChannelID) : IN_StoredIDs(), IN_MissingIDs(), IN_HighestID(0), Channel(Address, ChannelID) {}


		//	Receives an ordered packet
		//	LastID+1 here is the 'next expected packet'
		inline const bool Receive(ReceivePacket*const IN_Packet)
		{
#ifdef _PERF_SPINLOCK
			while (!In_Mutex.try_lock()) {}
#else
			In_Mutex.lock();
#endif
			//	If this ID was missing, remove it from the MissingIDs container
			if (IN_MissingIDs.count(IN_Packet->GetPacketID())) { IN_MissingIDs.erase(IN_Packet->GetPacketID()); }

			//	Ignore ID's below the LowestID
			if (IN_Packet->GetPacketID() <= In_LastID) { return true; }

			//	Update our HighestID if needed
			if (IN_Packet->GetPacketID() > IN_HighestID) { IN_HighestID = IN_Packet->GetPacketID(); }

			//	(in-sequence processing)
			//	Update our LowestID if needed
			if (IN_Packet->GetPacketID() == In_LastID + 1) {
				++In_LastID;
				printf("Ordered - %d - %s\tNew\n", IN_Packet->GetPacketID(), IN_Packet->ReadData<std::string>().c_str());
				// Loop through our StoredIDs container until we cant find (LowestID+1)
				while (IN_StoredIDs.count(In_LastID + 1))
				{
					++In_LastID;
					printf("Ordered - %d - %s\tStored\n", In_LastID.load(), IN_StoredIDs.at(In_LastID)->ReadData<std::string>().c_str());
					IN_StoredIDs.erase(In_LastID);
				}
				In_Mutex.unlock();
				return true;
			}

			//	(out-of-sequence processing)
			//	At this point ID must be greater than LowestID
			//	Which means we have an out-of-sequence ID
			IN_StoredIDs.emplace(IN_Packet->GetPacketID(), IN_Packet);
			for (unsigned long i = IN_Packet->GetPacketID() - 1; i > In_LastID; --i)
			{
				if (!IN_StoredIDs.count(i)) { IN_MissingIDs[i] = true; }
			}
			In_Mutex.unlock();
			return true;
		}
		//	Returns an unordered map of all the missing id's (this could include id's currently in transit)
		inline const unordered_map<unsigned long, bool> &GetMissingIDs() const { return IN_MissingIDs; }
	};
}