#pragma once

namespace PeerNet
{
	using std::chrono::duration_cast;
	using std::milli;

	class KeepAliveChannel
	{
		const NetAddress*const Address;
		const PacketType ChannelID;

		const long long RollingRTT;			//	Keep a rolling average of the last estimated 60 Round Trip Times
		duration<double, milli> OUT_RTT;	//	Start the system off assuming a 100ms ping. Let the algorythms adjust from that point.

		std::atomic<unsigned long> IN_LastID;	//	The largest received ID so far

		std::mutex OUT_Mutex;
		std::atomic<unsigned long> OUT_NextID;	//	Next packet ID we'll use
		std::atomic<unsigned long> OUT_LastACK;	//	Highest ACK'd packet id

	public:
		inline KeepAliveChannel(const NetAddress*const Addr, const PacketType &ChanID)
			: Address(Addr), ChannelID(ChanID), RollingRTT(60), OUT_RTT(100),
			IN_LastID(0),
			OUT_Mutex(), OUT_NextID(1), OUT_LastACK(0) {}

		//	Initialize and return a new packet for sending
		inline SendPacket*const NewPacket()
		{
			return new SendPacket(OUT_NextID++, ChannelID, 0, Address, true);
		}

		//	Receives a packet
		//	Returns true if we should send back an ACK
		inline const bool Receive(ReceivePacket*const IN_Packet)
		{
			//	If this is receive is an ACK
			if (IN_Packet->ReadData<bool>())
			{
				if (IN_Packet->GetPacketID() <= OUT_LastACK.load()) { return false; }
				OUT_LastACK.store(IN_Packet->GetPacketID());
				//	Calculate the RTT
				std::chrono::duration <double, milli> RTT = high_resolution_clock::now() - IN_Packet->GetCreationTime();
#ifdef _PERF_SPINLOCK
				while (!OUT_Mutex.try_lock()) {}
#else
				OUT_Mutex.lock();
#endif
				OUT_RTT -= OUT_RTT / RollingRTT;
				OUT_RTT += RTT / RollingRTT;
				OUT_Mutex.unlock();
				return false;
			}
			//	If this is a regular receive
			if (IN_Packet->GetPacketID() <= IN_LastID.load()) { return false; }
			IN_LastID.store(IN_Packet->GetPacketID());
			return true;
		}

		//	Get the largest received ID so far
		inline const auto GetLastID() const { return IN_LastID.load(); }

		//	Returns the Round-Trip-Time for Keep-Alive packets in milliseconds
		inline const auto RTT() const { return OUT_RTT; }

		//	Gets the current amount of unacknowledged packets
		inline const auto GetUnacknowledgedCount()
		{
			return OUT_NextID.load() - OUT_LastACK.load();
		}
	};
}