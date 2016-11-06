#pragma once

namespace PeerNet
{
	template <PacketType ChannelID>
	class KeepAliveChannel : public Channel
	{
		const double RollingRTT;	//	Keep a rolling average of the last estimated 15 Round Trip Times
		unordered_map<unsigned long, shared_ptr<NetPacket>> Out_Packets;	//	Outgoing packets that may need resent
		unsigned long Out_LastACK;	//	Most recent acknowledged ID
		double Out_RTT;				//	Start the system off assuming a 300ms ping. Let the algorythms adjust from that point.
	public:
		//	Default constructor initializes us and our base class
		KeepAliveChannel(NetPeer* ThisPeer) : RollingRTT(15), Out_LastACK(0), Out_RTT(0), Channel(ThisPeer) {}
		//	Initialize and return a new packet
		shared_ptr<NetPacket> NewPacket()
		{
			Out_Mutex.lock();
			shared_ptr<NetPacket> Packet = std::make_shared<NetPacket>(Out_NextID, ChannelID, MyPeer);
			Out_Packets[Out_NextID++] = Packet;
			Out_Mutex.unlock();
			return Packet;
		}
		//	Receives a packet
		const bool Receive(NetPacket* IN_Packet)
		{
			if (IN_Packet->ReadData<bool>())
			{
				In_Mutex.lock();
				if (IN_Packet->GetPacketID() <= In_LastID) { In_Mutex.unlock(); delete IN_Packet; return false; }
				In_LastID = IN_Packet->GetPacketID();
				In_Mutex.unlock();
				return true;
			}
			else
			{
				Out_Mutex.lock();
				if (Out_Packets.count(IN_Packet->GetPacketID()))
				{
					double RTT = duration<double, std::milli>(IN_Packet->GetCreationTime() - Out_Packets.at(IN_Packet->GetPacketID())->GetCreationTime()).count();
					Out_RTT -= Out_RTT / RollingRTT;
					Out_RTT += RTT / RollingRTT;
				}
				Out_Mutex.unlock(); delete IN_Packet; return false;
			}
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
				if (Packet.first <= Out_LastACK) { Out_Packets.erase(Packet.first); }
			}
			//	If their last received reliable ID is less than our last sent reliable id
			//	Send the most recently sent reliable packet to them again
			//	Note: if this packet is still in-transit it will be sent again
			//	ToDo:	Hold off on resending the packet until it's creation time
			//			is greater than this clients RTT
			const unsigned long Out_LastID = Out_NextID - 1;
			if (ID < Out_LastID && Out_Packets.count(Out_LastID)) { MyPeer->Socket->PostCompletion<NetPacket*>(CK_SEND, Out_Packets[Out_LastID].get()); }
			Out_Mutex.unlock();
		}

		const double RTT() const { return Out_RTT; }
	};
}