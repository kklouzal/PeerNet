#pragma once

namespace PeerNet
{
	template <PacketType ChannelID>
	class UnreliableChannel : public Channel
	{
	public:
		UnreliableChannel(NetPeer* ThisPeer) : Channel(ThisPeer) {}
		//	Initialize and return a new packet
		shared_ptr<NetPacket> NewPacket()
		{
			Out_Mutex.lock();
			shared_ptr<NetPacket> Packet = std::make_shared<NetPacket>(Out_NextID++, ChannelID, MyPeer);
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
		//	Update a remote peers acknowledgement
		void ACK(const unsigned long ID) { /*Unreliable Packets Do Nothing*/ }
	};
}