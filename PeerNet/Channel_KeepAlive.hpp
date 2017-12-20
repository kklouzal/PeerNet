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
		KeepAliveChannel(const NetAddress*const Address, const PacketType &ChannelID) : RollingRTT(20), Out_RTT(300), Channel(Address, ChannelID) {}

		//	Receives a packet
		inline const bool Receive(ReceivePacket*const IN_Packet)
		{
			if (IN_Packet->ReadData<bool>())
			{
				//In_Mutex.lock();
				if (IN_Packet->GetPacketID() <= In_LastID.load()) { /*In_Mutex.unlock();*/ delete IN_Packet; return false; }
				In_LastID.store(IN_Packet->GetPacketID());
				//In_Mutex.unlock();
				return true;
			}
			else
			{
				auto it = Out_Packets.find(IN_Packet->GetPacketID());
				if (it != Out_Packets.end())
				{
					duration<double> RTT = duration_cast<duration<double>>(IN_Packet->GetCreationTime() - it->second->GetCreationTime());
					Out_Mutex.lock();
					Out_RTT -= Out_RTT / RollingRTT;
					Out_RTT += RTT / RollingRTT;
					Out_Mutex.unlock();
				}
				delete IN_Packet; return false;
			}
		}

		//	Returns the Round-Trip-Time for Keep-Alive packets in milliseconds
		inline const auto RTT() const { return Out_RTT; }
	};
}