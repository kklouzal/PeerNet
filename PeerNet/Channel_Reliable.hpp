#pragma once

namespace PeerNet
{
	class ReliableChannel : public Channel
	{
	public:
		ReliableChannel(PacketType ChannelID) : Channel(ChannelID) {}

		//	Receives a packet
		const bool Receive(ReceivePacket* IN_Packet)
		{
			In_Mutex.lock();
			if (IN_Packet->GetPacketID() <= In_LastID) { In_Mutex.unlock(); delete IN_Packet; return false; }
			In_LastID = IN_Packet->GetPacketID();
			In_Mutex.unlock();
			return true;
		}
	};
}