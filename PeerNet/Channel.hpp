#pragma once
//#include "lz4.h"	//	Compression/Decompression Library

#include <mutex>
#include <memory>
#include <unordered_map>

using std::mutex;
using std::string;
using std::shared_ptr;
using std::make_shared;
using std::unordered_map;
using std::chrono::duration;
using std::chrono::time_point;
using std::chrono::high_resolution_clock;

namespace PeerNet
{
	class NetPeer;	// ToDo: Eliminate need to pass down NetPeer.

	//	Base Class Channel	-	Foundation for the other Channel types
	class Channel
	{
	protected:
		//	Main Variables
		NetPeer*const MyPeer;		//	ToDo: Eliminate need to pass down NetPeer
		PacketType ChannelID;

		//	Outgoing Variables
		mutex Out_Mutex;			//	Synchronize this channels Outgoing vars and funcs
		unsigned long Out_NextID;	//	Next packet ID we'll use
		//	Since we use shared pointers to manage memory cleanup for our packets
		//	Unreliable packets need to be held onto long enough to actually get sent
		unordered_map<unsigned long, shared_ptr<NetPacket>> Out_Packets;
		unsigned long Out_LastACK;	//	Most recent acknowledged ID

		//	Incoming Variables
		mutex In_Mutex;				//	Synchronize this channels Incoming vars and funcs
		unsigned long In_LastID;	//	The largest received ID so far
	public:
		//	Constructor initializes our base class
		Channel(NetPeer*const ThisPeer, PacketType ChanID)
			: MyPeer(ThisPeer), ChannelID(ChanID), Out_Mutex(), Out_NextID(1), Out_Packets(), Out_LastACK(0), In_Mutex(), In_LastID(0) {}
		//
		const auto GetChannelID() const { return ChannelID; }
		//	Initialize and return a new packet
		shared_ptr<NetPacket> NewPacket()
		{
			Out_Mutex.lock();
			shared_ptr<NetPacket> Packet = std::make_shared<NetPacket>(Out_NextID, GetChannelID(), MyPeer);
			Out_Packets[Out_NextID++] = Packet;
			Out_Mutex.unlock();
			return Packet;
		}
		//	Receives a packet
		virtual const bool Receive(NetPacket* IN_Packet) = 0;
		//	Get the largest received ID so far
		const auto GetLastID() const { return In_LastID; }
		//	Acknowledge delivery and processing of all packets up to this ID
		void ACK(const unsigned long ID)
		{
			//	We hold onto all the sent packets with an ID higher than that of
			//	Which our remote peer has not confirmed delivery for as those
			//	Packets may still be going through their initial sending process
			Out_Mutex.lock();
			if (ID > Out_LastACK)
			{
				Out_LastACK = ID;
				auto Out_Itr = Out_Packets.begin();
				while (Out_Itr != Out_Packets.end()) {
					if (Out_Itr->first <= Out_LastACK) {
						Out_Packets.erase(Out_Itr++);
					}
					else {
						++Out_Itr;
					}
				}
			}
			//	If their last received reliable ID is less than our last sent reliable id
			//	Send the most recently sent reliable packet to them again
			//	Note: if this packet is still in-transit it will be sent again
			//	ToDo:	Hold off on resending the packet until it's creation time
			//			is greater than this clients RTT
			//if (ID < Out_NextID - 1 && Out_Packets.count(Out_NextID - 1)) { MyPeer->Socket->PostCompletion<NetPacket*>(CK_SEND, Out_Packets[Out_NextID - 1].get()); }
			Out_Mutex.unlock();
		}

		/*virtual const string CompressPacket(NetPacket* OUT_Packet) = 0;
		virtual NetPacket* DecompressPacket(string IN_Data) = 0;*/
	};
}

#include "Channel_Ordered.hpp"
#include "Channel_Reliable.hpp"
#include "Channel_Unreliable.hpp"
#include "Channel_KeepAlive.hpp"