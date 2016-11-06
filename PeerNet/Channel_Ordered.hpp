#pragma once

namespace PeerNet
{
	template <PacketType ChannelID>
	class OrderedChannel : public Channel
	{
		unordered_map<unsigned long, shared_ptr<NetPacket>> Out_Packets;	//	Outgoing packets that may need resent
		unsigned long Out_LastACK;	//	Most recent acknowledged ID

		std::unordered_map<unsigned long, shared_ptr<NetPacket>> IN_OrderedPkts;	//	Incoming packets we cant process yet
		std::unordered_map<unsigned long, bool> IN_MissingIDs;	//	Missing IDs from the ordered sequence

		unsigned int IN_HighestID;	//	Highest received ID
	public:
		//	Default constructor initializes us and our base class
		OrderedChannel(NetPeer* ThisPeer) : Out_LastACK(0), Channel(ThisPeer) {}
		//	Initialize and return a new packet
		shared_ptr<NetPacket> NewPacket()
		{
			Out_Mutex.lock();
			Out_Packets[Out_NextID] = std::make_shared<NetPacket>(Out_NextID, ChannelID, MyPeer);
			Out_Mutex.unlock();
			return Out_Packets[Out_NextID++];
		}
		//	Receives an ordered packet
		//	LastID+1 here is the 'next expected packet'
		const bool Receive(NetPacket* IN_Packet)
		{
			In_Mutex.lock();
			//	If this ID was missing, remove it from the MissingIDs container
			if (IN_MissingIDs.count(IN_Packet->GetPacketID())) { IN_MissingIDs.erase(IN_Packet->GetPacketID()); }

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
#ifdef _DEBUG_PACKETS_ORDERED
				printf("Process Ordered Packet - %u\n", IN_Packet->GetPacketID());
#endif
				//	Check the container against our new counter value
				while (!IN_OrderedPkts.empty())
				{
					//	See if the next expected packet is in our container
					auto got = IN_OrderedPkts.find(In_LastID + 1);
					//	Not found; quit searching, unlock and return
					if (got == IN_OrderedPkts.end()) { In_Mutex.unlock(); return true; }
					//	Found; increment counter; process packet; continue searching
					++In_LastID;
#ifdef _DEBUG_PACKETS_ORDERED
					printf("\tProcess Stored Packet - %u\n", got->first);
#endif
					IN_OrderedPkts.erase(got);
				}
			}
			else { delete IN_Packet; return false; }
			//	Searching complete, Unlock and return
			In_Mutex.unlock();
			return true;
		}
		//	Acknowledge the remote peers reception of an ID
		void ACK(const unsigned long ID)
		{
			Out_Mutex.lock();
			//	If this ID is less than or equal to the most recent ACK, disreguard it
			if (ID <= Out_LastACK) { Out_Mutex.unlock(); return; }
			//	Remove any packets with an ID less or equal to our most recently acknowledged ID
			Out_LastACK = ID;
			for (auto Packet : Out_Packets)
			{
				if (Packet.first < Out_LastACK) { Out_Packets.erase(Packet.first); }
				else if (Packet.first == Out_LastACK)
				{
					Out_Packets.erase(Packet.first);
				}
			}
			//	If their last received reliable ID is less than our last sent reliable id
			//	Send the most recently sent reliable packet to them again
			//	Note: if this packet is still in-transit it will be sent again
			//	ToDo:	Hold off on resending the packet until it's creation time
			//			is greater than this clients RTT
			if (ID < Out_NextID - 1 && Out_Packets.count(Out_NextID - 1)) { MyPeer->Socket->PostCompletion<NetPacket*>(CK_SEND, Out_Packets[Out_NextID-1].get()); }
			Out_Mutex.unlock();
		}
		//	Returns an unordered map of all the missing id's (this could include id's currently in transit)
		const unordered_map<unsigned long, bool> GetMissingIDs() const { return MissingIDs; }
	};
}