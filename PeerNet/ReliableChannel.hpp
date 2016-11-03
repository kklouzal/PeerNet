#pragma once
#include <mutex>
#include <unordered_map>

using std::mutex;
using std::unordered_map;
using std::chrono::time_point;
using std::chrono::high_resolution_clock;

namespace PeerNet
{
	class NetPeer;	// ToDo: Eliminate need to pass down NetPeer.

	//	Base Class Channel	-	Foundation for the other Channel types
	class Channel
	{
	protected:
		NetPeer* MyPeer;			//	ToDo: Eliminate need to pass down NetPeer
		mutex Out_Mutex;			//	Synchronize this channels Outgoing vars and funcs
		unsigned long Out_NextID;	//	Next packet ID we'll use
		mutex In_Mutex;				//	Synchronize this channels Incoming vars and funcs
		unsigned long In_LastID;	//	The largest received ID so far
	public:
		//	Constructor initializes our base class
		Channel(NetPeer* ThisPeer) : MyPeer(ThisPeer), Out_Mutex(), Out_NextID(1), In_Mutex(), In_LastID(0) {}
		//	Initialize and return a new packet
		virtual NetPacket* NewPacket() = 0;
		//	Receives a packet
		virtual const bool Receive(NetPacket* IN_Packet) = 0;
		//	Update a remote peers acknowledgement
		virtual void ACK(const unsigned long ID, const time_point<high_resolution_clock> AckTime) = 0;
		//	Get the largest received ID so far
		const unsigned long GetLastID() const { return In_LastID; }
	};

	template <PacketType ChannelID>
	class UnreliableChannel : public Channel
	{
	public:
		UnreliableChannel(NetPeer* ThisPeer) : Channel(ThisPeer) {}
		//	Initialize and return a new packet
		NetPacket* NewPacket()
		{
			Out_Mutex.lock();
			NetPacket* Packet = new NetPacket(Out_NextID++, ChannelID, MyPeer);
			Out_Mutex.unlock();
			return Packet;
		}
		//	Receives a packet
		const bool Receive(NetPacket* IN_Packet)
		{
			In_Mutex.lock();
			if (IN_Packet->GetPacketID() <= In_LastID) { In_Mutex.unlock(); return false; }
			In_LastID = IN_Packet->GetPacketID();
			In_Mutex.unlock();
			return true;
		}
		//	Update a remote peers acknowledgement
		void ACK(const unsigned long ID, const time_point<high_resolution_clock> AckTime) { /*Unreliable Packets Do Nothing*/ }
	};

	template <PacketType ChannelID>
	class ReliableChannel : public Channel
	{
		const double RollingRTT;	//	Keep a rolling average of the last estimated 15 Round Trip Times
		unordered_map<unsigned long, NetPacket*> Out_Packets;	//	Outgoing packets that may need resent
		unsigned long Out_LastACK;	//	Most recent acknowledged ID
		double Out_RTT;				//	Start the system off assuming a 300ms ping. Let the algorythms adjust from that point.
	public:
		//	Default constructor initializes us and our base class
		ReliableChannel(NetPeer* ThisPeer) : RollingRTT(15), Out_LastACK(0), Out_RTT(0), Channel(ThisPeer) {}
		//	Initialize and return a new packet
		virtual NetPacket* NewPacket()
		{
			Out_Mutex.lock();
			NetPacket* Packet = new NetPacket(Out_NextID, ChannelID, MyPeer);
			Out_Packets[Out_NextID++] = Packet;
			Out_Mutex.unlock();
			return Packet;
		}
		//	Receives a packet
		virtual const bool Receive(NetPacket* IN_Packet)
		{
			In_Mutex.lock();
			if (IN_Packet->GetPacketID() <= In_LastID) { In_Mutex.unlock(); return false; }
			In_LastID = IN_Packet->GetPacketID();
			In_Mutex.unlock();
			return true;
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
	};

	template <PacketType ChannelID>
	class OrderedChannel : public Channel
	{
		const double RollingRTT;	//	Keep a rolling average of the last estimated 15 Round Trip Times
		unordered_map<unsigned long, NetPacket*> Out_Packets;	//	Outgoing packets that may need resent
		unsigned long Out_LastACK;	//	Most recent acknowledged ID
		double Out_RTT;				//	Start the system off assuming a 300ms ping. Let the algorythms adjust from that point.

		std::unordered_map<unsigned long, NetPacket*const> IN_OrderedPkts;	//	Incoming packets we cant process yet
	public:
		//	Default constructor initializes us and our base class
		OrderedChannel(NetPeer* ThisPeer) : RollingRTT(15), Out_LastACK(0), Out_RTT(0), Channel(ThisPeer) {}
		//	Initialize and return a new packet
		virtual NetPacket* NewPacket()
		{
			Out_Mutex.lock();
			NetPacket* Packet = new NetPacket(Out_NextID, ChannelID, MyPeer);
			Out_Packets[Out_NextID++] = Packet;
			Out_Mutex.unlock();
			return Packet;
		}
		//	Receives an ordered packet
		//	LastID+1 here is the 'next expected packet'
		virtual const bool Receive(NetPacket* IN_Packet)
		{
			In_Mutex.lock();
			if (IN_Packet->GetPacketID() > In_LastID+1)
			{
#ifdef _DEBUG_PACKETS_ORDERED
				printf("Store Ordered Packet %u - Needed %u\n", IN_Packet->GetPacketID(), In_LastID+1);
#endif
				IN_OrderedPkts.emplace(IN_Packet->GetPacketID(), IN_Packet);
				In_Mutex.unlock();
				return false;
			}
			else if (IN_Packet->GetPacketID() == In_LastID+1)
			{
				++In_LastID;
#ifdef _DEBUG_PACKETS_ORDERED
				printf("Process Ordered Packet - %u\n", IN_Packet->GetPacketID());
#endif
				//	Check the container against our new counter value
				while (!IN_OrderedPkts.empty())
				{
					//	See if the next expected packet is in our container
					auto got = IN_OrderedPkts.find(In_LastID + 1);
					//	Not found; quit searching, unlock and return
					if (got == IN_OrderedPkts.end()) { In_Mutex.unlock(); return true; }
					//	Found; increment counter; process packet; continue searching
					++In_LastID;
#ifdef _DEBUG_PACKETS_ORDERED
					printf("\tProcess Stored Packet - %u\n", got->first);
#endif
					delete got->second;
					IN_OrderedPkts.erase(got);
				}
			}
			//	Searching complete, Unlock and return
			In_Mutex.unlock();
			return true;
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
			if (ID < Out_NextID - 1 && Out_Packets.count(Out_NextID - 1)) { MyPeer->Socket->PostCompletion<NetPacket*const>(CK_SEND, Out_Packets[Out_NextID - 1]); }
			Out_Mutex.unlock();
		}

		const double RTT() const { return Out_RTT; }
	};
}