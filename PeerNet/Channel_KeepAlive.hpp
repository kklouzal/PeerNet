#pragma once

namespace PeerNet
{
	class KeepAliveChannel : public Channel
	{
		const double RollingRTT;	//	Keep a rolling average of the last estimated 20 Round Trip Times
									//	- That should equate to about 5 seconds worth of averaging with a 250ms average RTT
		double Out_RTT;				//	Start the system off assuming a 300ms ping. Let the algorythms adjust from that point.
	public:
		//	Default constructor initializes us and our base class
		KeepAliveChannel(NetPeer* ThisPeer, PacketType ChannelID) : RollingRTT(35), Out_RTT(0), Channel(ThisPeer, ChannelID) {}

		//	Receives a packet
		const bool Receive(ReceivePacket* IN_Packet)
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

		//	Returns the Round-Trip-Time for Keep-Alive packets in milliseconds
		const double RTT() const { return Out_RTT; }
	};
}