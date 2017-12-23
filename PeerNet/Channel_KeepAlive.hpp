#pragma once

namespace PeerNet
{
	using std::chrono::duration_cast;
	using std::milli;

	class KeepAliveChannel
	{
		const NetAddress*const Address;
		const PacketType ChannelID;

		const long long RollingRTT;			//	Keep a rolling average of the last estimated 20 Round Trip Times
		duration<double, milli> OUT_RTT;	//	Start the system off assuming a 100ms ping. Let the algorythms adjust from that point.

		std::atomic<unsigned long> IN_LastID;	//	The largest received ID so far

		std::mutex OUT_Mutex;
		std::atomic<unsigned long> OUT_NextID;	//	Next packet ID we'll use
		std::atomic<unsigned long> OUT_LastACK;	//	Highest ACK'd packet id
		std::atomic<size_t> OUT_CurAmount;		//	Current amount of unacknowledged packets
		std::unordered_map<unsigned long, const std::shared_ptr<NetPacket>> OUT_Packets;	//	Unacknowledged outgoing packets

	public:
		KeepAliveChannel(const NetAddress*const Addr, const PacketType &ChanID)
			: Address(Addr), ChannelID(ChanID), RollingRTT(20), OUT_RTT(100),
			IN_LastID(0),
			OUT_Mutex(), OUT_NextID(1), OUT_LastACK(0), OUT_CurAmount(0), OUT_Packets(0) {}

		//	Acknowledge all packets up to this ID
		inline void ACK(const unsigned long& ID)
		{
			//	We hold onto all the sent packets with an ID higher than that of
			//	Which our remote peer has not confirmed delivery for as those
			//	Packets may still be going through their initial sending process
			if (ID > OUT_LastACK)
			{
				OUT_LastACK = ID;
#ifdef _PERF_SPINLOCK
				while (!OUT_Mutex.try_lock()) {}
#else
				OUT_Mutex.lock();
#endif
				auto it = OUT_Packets.begin();
				while (it != OUT_Packets.end()) {
					if (it->first <= OUT_LastACK.load()) {
						OUT_Packets.erase(it++);
						continue;
					}
					++it;
				}
				OUT_CurAmount.store(OUT_Packets.size());
				OUT_Mutex.unlock();
			}
			//	If their last received reliable ID is less than our last sent reliable id
			//	Send the most recently sent reliable packet to them again
			//	Note: if this packet is still in-transit it will be sent again
			//	ToDo:	Hold off on resending the packet until it's creation time
			//			is greater than this clients RTT
			//if (ID < Out_NextID - 1 && Out_Packets.count(Out_NextID - 1)) { MyPeer->Socket->PostCompletion<NetPacket*>(CK_SEND, Out_Packets[Out_NextID - 1].get()); }
		}

		//	Initialize and return a new packet for sending
		inline std::shared_ptr<SendPacket> NewPacket()
		{
			std::shared_ptr<SendPacket> Packet = std::make_shared<SendPacket>(OUT_NextID.load(), ChannelID, Address);
#ifdef _PERF_SPINLOCK
			while (!OUT_Mutex.try_lock()) {}
#else
			OUT_Mutex.lock();
#endif
			OUT_Packets.emplace(OUT_NextID++, Packet);
			OUT_CurAmount.store(OUT_Packets.size());
			OUT_Mutex.unlock();
			return Packet;
		}

		//	Receives a packet
		inline const bool Receive(ReceivePacket*const IN_Packet)
		{
			if (IN_Packet->ReadData<bool>())
			{
				if (IN_Packet->GetPacketID() <= IN_LastID.load()) { delete IN_Packet; return false; }
				IN_LastID.store(IN_Packet->GetPacketID());
				return true;
			}
			else
			{
				auto it = OUT_Packets.find(IN_Packet->GetPacketID());
				if (it != OUT_Packets.end())
				{
					duration<double> RTT = duration_cast<duration<double>>(IN_Packet->GetCreationTime() - it->second->GetCreationTime());
#ifdef _PERF_SPINLOCK
					while (!OUT_Mutex.try_lock()) {}
#else
					OUT_Mutex.lock();
#endif
					OUT_RTT -= OUT_RTT / RollingRTT;
					OUT_RTT += RTT / RollingRTT;
					OUT_Mutex.unlock();
				}
				delete IN_Packet; return false;
			}
		}

		//	Get the largest received ID so far
		inline const auto GetLastID() const { return IN_LastID.load(); }

		//	Returns the Round-Trip-Time for Keep-Alive packets in milliseconds
		inline const auto RTT() const { return OUT_RTT; }

		//	Gets the current amount of unacknowledged packets
		inline const auto GetUnacknowledgedCount() { return OUT_CurAmount.load(); }
	};
}