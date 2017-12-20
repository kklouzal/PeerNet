#pragma once

namespace PeerNet
{
	class ReliableChannel : public Channel
	{
	public:
		ReliableChannel(NetPeer* ThisPeer, PacketType ChannelID) : Channel(ThisPeer, ChannelID) {}

		//	Receives a packet
		const bool Receive(ReceivePacket*const IN_Packet)
		{
			if (IN_Packet->GetPacketID() <= In_LastID.load()) { delete IN_Packet; return false; }
			In_LastID.store(IN_Packet->GetPacketID());
			return true;
		}
	};
}