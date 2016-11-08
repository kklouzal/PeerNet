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
			shared_ptr<NetPacket> Packet = std::make_shared<NetPacket>(Out_NextID, ChannelID, MyPeer);
			Out_Packets[Out_NextID++] = Packet;
			Out_Mutex.unlock();
			return Packet;
		}
		//	Receives a packet
		const bool Receive(NetPacket* IN_Packet)
		{
			In_Mutex.lock();
			if (IN_Packet->GetPacketID() <= In_LastID) { In_Mutex.unlock(); /*delete IN_Packet;*/ return false; }
			In_LastID = IN_Packet->GetPacketID();
			In_Mutex.unlock();
			return true;
		}
		//	Update a remote peers acknowledgement
		void ACK(const unsigned long ID)
		{
			//	Unreliable Packets will never be resent for any reason, however
			//	We hold onto all the sent packets with an ID higher than that of
			//	Which our remote peer has not confirmed delivery for as those
			//	Packets may still be going through their initial sending process
			Out_Mutex.lock();
			if (ID > Out_LastACK)
			{
				Out_LastACK = ID;
				for (auto Packet : Out_Packets)
				{
					if (Packet.first <= Out_LastACK) { Out_Packets.erase(Packet.first); }
				}
			}
			Out_Mutex.unlock();
		}
	};
}