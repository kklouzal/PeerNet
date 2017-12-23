#pragma once

namespace PeerNet
{
	class UnreliableChannel
	{
		const NetAddress*const Address;
		const PacketType ChannelID;

		std::atomic<unsigned long> IN_LastID;	//	The largest received ID so far

		std::mutex OUT_Mutex;
		std::atomic<unsigned long> OUT_NextID;	//	Next packet ID we'll use
		std::atomic<unsigned long> OUT_LastACK;	//	Next packet ID we'll use
		std::unordered_map<unsigned long, const std::shared_ptr<NetPacket>> OUT_Packets;	//	Unacknowledged outgoing packets

	public:
		UnreliableChannel(const NetAddress*const Addr, const PacketType &ChanID)
			: Address(Addr), ChannelID(ChanID),
			IN_LastID(0),
			OUT_Mutex(), OUT_NextID(1), OUT_LastACK(0), OUT_Packets(0) {}

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
			OUT_Mutex.unlock();
			return Packet;
		}

		//	Receives a packet
		inline const bool Receive(ReceivePacket*const IN_Packet)
		{
			if (IN_Packet->GetPacketID() <= IN_LastID.load()) { delete IN_Packet; return false; }
			IN_LastID.store(IN_Packet->GetPacketID());
			return true;
		}

		//	Get the largest received ID so far
		inline const auto GetLastID() const { return IN_LastID.load(); }
	};
}