#pragma once

namespace PeerNet
{
	using std::chrono::duration_cast;
	using std::milli;

	class KeepAliveChannel : public Channel
	{
		const long long RollingRTT;			//	Keep a rolling average of the last estimated 20 Round Trip Times
											//	- That should equate to about 5 seconds worth of averaging with a 250ms average RTT
		duration<double, milli> Out_RTT;	//	Start the system off assuming a 300ms ping. Let the algorythms adjust from that point.
	public:
		//	Default constructor initializes us and our base class
		KeepAliveChannel(NetPeer* ThisPeer, PacketType ChannelID) : RollingRTT(20), Out_RTT(300), Channel(ThisPeer, ChannelID) {}

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
					duration<double> RTT = duration_cast<duration<double>>(IN_Packet->GetCreationTime() - Out_Packets.at(IN_Packet->GetPacketID())->GetCreationTime());
					Out_RTT -= Out_RTT / RollingRTT;
					Out_RTT += RTT / RollingRTT;
				}
				Out_Mutex.unlock(); delete IN_Packet; return false;
			}
		}

		//	Returns the Round-Trip-Time for Keep-Alive packets in milliseconds
		const auto RTT() const { return Out_RTT; }
	};
}