#pragma once

namespace PeerNet
{
	template <PacketType ChannelID>
	class ReliableChannel : public Channel
	{
		unordered_map<unsigned long, shared_ptr<NetPacket>> Out_Packets;	//	Outgoing packets that may need resent
		unsigned long Out_LastACK;	//	Most recent acknowledged ID
	public:
		//	Default constructor initializes us and our base class
		ReliableChannel(NetPeer* ThisPeer) : Out_LastACK(0), Channel(ThisPeer) {}
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
			In_Mutex.lock();
			if (IN_Packet->GetPacketID() <= In_LastID) { In_Mutex.unlock(); delete IN_Packet; return false; }
			In_LastID = IN_Packet->GetPacketID();
			In_Mutex.unlock();
			return true;
		}
		//	Acknowledge the remote peers reception of an ID
		void ACK(const unsigned long ID)
		{
			Out_Mutex.lock();
			//	If this ID is less than or equal to the most recent ACK, discard it
			if (ID <= Out_LastACK) { Out_Mutex.unlock(); return; }
			//	Remove any packets with an ID less or equal to our most recently acknowledged ID
			Out_LastACK = ID;
			for (auto Packet : Out_Packets)
			{
				if (Packet.first < Out_LastACK) { Out_Packets.erase(Packet.first); }
			}
			//	If their last received reliable ID is less than our last sent reliable id
			//	Send the most recently sent reliable packet to them again
			//	Note: if this packet is still in-transit it will be sent again
			//	ToDo:	Hold off on resending the packet until it's creation time
			//			is greater than this clients RTT
			if (ID < Out_NextID - 1 && Out_Packets.count(Out_NextID - 1)) { MyPeer->Socket->PostCompletion<NetPacket*>(CK_SEND, Out_Packets[Out_NextID-1].get()); }
			Out_Mutex.unlock();
		}
	};
}