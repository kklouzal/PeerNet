#pragma once

namespace PeerNet
{
	class UnreliableChannel : public Channel
	{
	public:
		UnreliableChannel(NetAddress* Address, PacketType ChannelID) : Channel(Address, ChannelID) {}
		//	Receives a packet
		const bool Receive(ReceivePacket*const IN_Packet)
		{
			if (IN_Packet->GetPacketID() <= In_LastID.load()) { delete IN_Packet; return false; }
			In_LastID.store(IN_Packet->GetPacketID());
			return true;
		}
	};
}