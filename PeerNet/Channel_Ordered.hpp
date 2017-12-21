#pragma once

namespace PeerNet
{
	class OrderedChannel : public Channel
	{
		std::unordered_map<unsigned long, shared_ptr<ReceivePacket>> IN_OrderedPkts;	//	Incoming packets we cant process yet
		std::unordered_map<unsigned long, bool> IN_MissingIDs;	//	Missing IDs from the ordered sequence

		unsigned int IN_HighestID;	//	Highest received ID

		bool Test = false;
	public:
		//	Default constructor initializes us and our base class
		OrderedChannel(const NetAddress*const Address, const PacketType &ChannelID) : IN_OrderedPkts(), IN_MissingIDs(), IN_HighestID(0), Channel(Address, ChannelID) {}

		/*inline void RetransmitFailedPackets()
		{
			In_Mutex.lock();
			Out_Mutex.lock();
			for (auto ID : IN_MissingIDs) {
				
			}
			Out_Mutex.unlock();
			In_Mutex.unlock();
		}*/

		//	Receives an ordered packet
		//	LastID+1 here is the 'next expected packet'
		inline const bool Receive(ReceivePacket*const IN_Packet)
		{
			if (Test && IN_Packet->GetPacketID() > 100 && IN_Packet->GetPacketID() < 900) {
				delete IN_Packet;
				Test = false;
				return false;
			}
#ifdef _PERF_SPINLOCK
			while (!In_Mutex.try_lock()) {}
#else
			In_Mutex.lock();
#endif
			//	If this ID was missing, remove it from the MissingIDs container
			auto it = IN_MissingIDs.find(IN_Packet->GetPacketID());
			if (it != IN_MissingIDs.end()) {
				IN_MissingIDs.erase(it);
			}

			if (IN_Packet->GetPacketID() > In_LastID + 1)
			{
#ifdef _DEBUG_PACKETS_ORDERED
				printf("Store Ordered Packet %u - Needed %u\n", IN_Packet->GetPacketID(), In_LastID + 1);
#endif
				if (IN_Packet->GetPacketID() > IN_HighestID) { IN_HighestID = IN_Packet->GetPacketID(); }

				IN_OrderedPkts.emplace(IN_Packet->GetPacketID(), IN_Packet);
				//	Recalculate our Missing ID's
				for (unsigned long i = IN_Packet->GetPacketID() - 1; i > In_LastID; --i)
				{
					if (!IN_OrderedPkts.count(i)) { IN_MissingIDs[i] = true; }
				}
				In_Mutex.unlock();
				return false;
			}
			else if (IN_Packet->GetPacketID() == In_LastID + 1)
			{
				++In_LastID;
//#ifdef _DEBUG_PACKETS_ORDERED
				printf("Ordered - %d - %s\tNew\n", IN_Packet->GetPacketID(), IN_Packet->ReadData<std::string>().c_str());
//#endif
				delete IN_Packet;	//	Cleanup the NetPacket's memory
				//	Check the container against our new counter value
				while (!IN_OrderedPkts.empty())
				{
					//	See if the next expected packet is in our container
					auto got = IN_OrderedPkts.find(In_LastID + 1);
					//	Not found; quit searching, unlock and return
					if (got == IN_OrderedPkts.end()) { In_Mutex.unlock(); return true; }
					//	Found; increment counter; process packet; continue searching
					++In_LastID;
//#ifdef _DEBUG_PACKETS_ORDERED
					printf("Ordered - %d - %s\tStored\n", got->first, got->second->ReadData<std::string>().c_str());
//#endif
					IN_OrderedPkts.erase(got);
				}
			}
			else { delete IN_Packet; return false; }
			//	Searching complete
			In_Mutex.unlock();
			return true;
		}
		//	Returns an unordered map of all the missing id's (this could include id's currently in transit)
		inline const unordered_map<unsigned long, bool> &GetMissingIDs() const { return IN_MissingIDs; }
	};
}