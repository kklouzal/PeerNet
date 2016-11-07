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

#include "Channel_Ordered.hpp"
#include "Channel_Reliable.hpp"
#include "Channel_Unreliable.hpp"
#include "Channel_KeepAlive.hpp"

namespace PeerNet
{
	class NetPeer;	// ToDo: Eliminate need to pass down NetPeer.

	//	Base Class Channel	-	Foundation for the other Channel types
	class Channel
	{
	protected:
		NetPeer*const MyPeer;		//	ToDo: Eliminate need to pass down NetPeer
		//
		mutex Out_Mutex;			//	Synchronize this channels Outgoing vars and funcs
		unsigned long Out_NextID;	//	Next packet ID we'll use
		mutex In_Mutex;				//	Synchronize this channels Incoming vars and funcs
		unsigned long In_LastID;	//	The largest received ID so far
	public:
		//	Constructor initializes our base class
		Channel(NetPeer*const ThisPeer) : MyPeer(ThisPeer), Out_Mutex(), Out_NextID(1), In_Mutex(), In_LastID(0) {}
		//	Initialize and return a new packet
		virtual shared_ptr<NetPacket> NewPacket() = 0;
		//	Receives a packet
		virtual const bool Receive(NetPacket* IN_Packet) = 0;
		//	Update a remote peers acknowledgement
		virtual void ACK(const unsigned long ID) = 0;
		//	Get the largest received ID so far
		const auto GetLastID() const { return In_LastID; }

		/*virtual const string CompressPacket(NetPacket* OUT_Packet) = 0;
		virtual NetPacket* DecompressPacket(string IN_Data) = 0;*/
	};
}