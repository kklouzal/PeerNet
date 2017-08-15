#pragma once

namespace PeerNet
{
	class OrderedChannel : public Channel
	{
		std::unordered_map<unsigned long, shared_ptr<ReceivePacket>> IN_OrderedPkts;	//	Incoming packets we cant process yet
		std::unordered_map<unsigned long, bool> IN_MissingIDs;	//	Missing IDs from the ordered sequence

		unsigned int IN_HighestID;	//	Highest received ID
	public:
		//	Default constructor initializes us and our base class
		OrderedChannel(PacketType ChannelID) : IN_OrderedPkts(), IN_MissingIDs(), IN_HighestID(0), Channel(ChannelID) {}

		//	Receives an ordered packet
		//	LastID+1 here is the 'next expected packet'
		const bool Receive(ReceivePacket* IN_Packet)
		{
			In_Mutex.lock();
			//	If this ID was missing, remove it from the MissingIDs container
			if (IN_MissingIDs.count(IN_Packet->GetPacketID())) { IN_MissingIDs.erase(IN_Packet->GetPacketID()); }

			if (IN_Packet->GetPacketID() > In_LastID + 1)
			{
				//printf("Store Ordered Packet %u - Needed %u\n", IN_Packet->GetPacketID(), In_LastID + 1);
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
				Log("Ordered - " + std::to_string(IN_Packet->GetPacketID()) + " - " + IN_Packet->ReadData<std::string>() + "\tNew\n");
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
					Log("Ordered - " + std::to_string(got->first) + " - " + got->second->ReadData<std::string>() + "\tStored\n");
					IN_OrderedPkts.erase(got);
				}
			}
			else { delete IN_Packet; return false; }
			//	Searching complete, Unlock and return
			In_Mutex.unlock();
			return true;
		}
		//	Returns an unordered map of all the missing id's (this could include id's currently in transit)
		const unordered_map<unsigned long, bool> GetMissingIDs() const { return IN_MissingIDs; }
	};
}