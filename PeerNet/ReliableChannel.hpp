#pragma once
#include <mutex>
#include <unordered_map>

using std::mutex;
using std::unordered_map;

//	ReliableChannel

namespace PeerNet
{
	class NetPeer;	// ToDo: Eliminate need to pass down NetPeer.

	template <PacketType ChannelID>
	class UnreliableChannel
	{
		NetPeer* MyPeer;	// ToDo: Eliminate need to pass down NetPeer.

		mutex Out_Mutex;
		unsigned long Out_NextID;	//	Next packet ID we'll use

		mutex In_Mutex;
		unsigned long In_LastID;
	public:
		UnreliableChannel(NetPeer* ThisPeer) : MyPeer(ThisPeer), In_LastID(0), Out_NextID(1) {}
		//	Constructs and returns a new NetPacket with the proper Packet ID
		NetPacket* NewPacket()
		{
			//	ToDo: Construct the NetPacket outside our lock, 
			Out_Mutex.lock();
			NetPacket* Packet = new NetPacket(Out_NextID++, ChannelID, MyPeer);
			Out_Mutex.unlock();
			return Packet;
		}
		const bool Receive(const unsigned short ID)
		{
			In_Mutex.lock();
			if (ID <= In_LastID) { In_Mutex.unlock(); return false; }
			In_LastID = ID;
			In_Mutex.unlock();
			return true;
		}
	};

	template <PacketType ChannelID>
	class ReliableChannel
	{
		NetPeer* MyPeer;	// ToDo: Eliminate need to pass down NetPeer.
		const double RollingRTT = 15;	//	Keep a rolling average of the last estimated 15 Round Trip Times

		mutex Out_Mutex;
		unordered_map<unsigned long, NetPacket*> Out_Packets;
		unsigned long Out_NextID;	//	Next packet ID we'll use
		unsigned long Out_LastACK;	//	Most recent acknowledged ID
		double Out_RTT = 0;			//	Start the system off assuming a 300ms ping. Let the algorythms adjust from that point.

		mutex In_Mutex;
		unsigned long In_LastID;

	public:
		//	Returns the most recent received ID
		const unsigned long GetLastID() const { return In_LastID; }

		ReliableChannel(NetPeer* ThisPeer) : MyPeer(ThisPeer), In_LastID(0), Out_NextID(1), Out_LastACK(0) {}

		//	Constructs and returns a new NetPacket with the proper Packet ID
		NetPacket* NewPacket()
		{
			//	ToDo: Construct the NetPacket outside our lock, 
			Out_Mutex.lock();
			NetPacket* Packet = new NetPacket(Out_NextID, ChannelID, MyPeer);
			Out_Packets[Out_NextID++] = Packet;
			Out_Mutex.unlock();
			return Packet;
		}

		//	Acknowledge the remote peers reception of an ID
		void ACK(const unsigned long ID, const std::chrono::time_point<std::chrono::high_resolution_clock> AckTime)
		{
			Out_Mutex.lock();
			//	If this ID is less than or equal to the most recent ACK, disreguard it
			if (ID <= Out_LastACK) { Out_Mutex.unlock(); return; }
			//	Remove any packets with an ID less or equal to our most recently acknowledged ID
			Out_LastACK = ID;
			for (auto Packet : Out_Packets)
			{
				if (Packet.first < Out_LastACK) { Out_Packets.erase(Packet.first); }
				else if (Packet.first == Out_LastACK)
				{
					double RTT = std::chrono::duration<double, std::milli>(AckTime - Packet.second->GetCreationTime()).count();
					Out_RTT -= Out_RTT / RollingRTT;
					Out_RTT += RTT / RollingRTT;
					Out_Packets.erase(Packet.first);
				}
			}
			//	If their last received reliable ID is less than our last sent reliable id
			//	Send the most recently sent reliable packet to them again
			//	Note: if this packet is still in-transit it will be sent again
			//	ToDo:	Hold off on resending the packet until it's creation time
			//			is greater than this clients RTT
			if (ID < Out_NextID-1 && Out_Packets.count(Out_NextID-1)) { MyPeer->Socket->PostCompletion<NetPacket*const>(CK_SEND, Out_Packets[Out_NextID-1]); }
			Out_Mutex.unlock();
		}

		const double RTT() const { return Out_RTT; }

		const bool Receive(const unsigned short ID)
		{
			In_Mutex.lock();
			if (ID <= In_LastID) { In_Mutex.unlock(); return false; }
			In_LastID = ID;
			In_Mutex.unlock();
			return true;
		}
	};
}