#pragma once
#include "TimedEvent.hpp"

namespace PeerNet
{
	class NetPacket
	{
		NetPeer*const MyPeer;		//	The destination peer for this packet

		std::chrono::time_point<std::chrono::high_resolution_clock> CreationTime;
		std::chrono::time_point<std::chrono::high_resolution_clock> NextSendTime;

		unsigned int PacketID;
		PeerNet::PacketType TypeID;

		std::stringstream DataStream;
		cereal::PortableBinaryOutputArchive*const BinaryIn;
		cereal::PortableBinaryInputArchive*const BinaryOut;
		// Is this a reliable packet
		const auto IsReliable() const { return ((TypeID == PacketType::PN_Ordered) || (TypeID == PacketType::PN_Reliable)); }

	public:
		unsigned short SendAttempts;

		// This constructor is for handling Receive Packets ONLY
		NetPacket(const std::string Data)
			: DataStream(Data, std::ios::in | std::ios::out | std::ios::binary), BinaryIn(nullptr),
			BinaryOut(new cereal::PortableBinaryInputArchive(DataStream)), SendAttempts(0), MyPeer(nullptr)
		{
			BinaryOut->operator()(PacketID);
			BinaryOut->operator()(TypeID);
			if (TypeID == PacketType::PN_ReliableACK || TypeID == PacketType::PN_OrderedACK) { CreationTime = std::chrono::high_resolution_clock::now(); }
		}

		// This constructor is for handling Send Packets ONLY
		NetPacket(const unsigned long pID, const PeerNet::PacketType pType, NetPeer*const Peer)
			: PacketID(pID), TypeID(pType), DataStream(std::ios::in | std::ios::out | std::ios::binary), BinaryIn(new cereal::PortableBinaryOutputArchive(DataStream)),
			BinaryOut(nullptr), SendAttempts(0), MyPeer(Peer)
		{
			BinaryIn->operator()(PacketID);
			BinaryIn->operator()(TypeID);
			if (IsReliable()) {
				CreationTime = std::chrono::high_resolution_clock::now();
				NextSendTime = CreationTime + std::chrono::milliseconds(1800);
			}
		}

		//	Default Destructor
		~NetPacket()
		{
			if (BinaryIn != nullptr) { delete BinaryIn; }
			if (BinaryOut != nullptr) { delete BinaryOut; }
		}

		// Write data into the packet
		template <typename T> void WriteData(T Data) const { BinaryIn->operator()(Data); }

		// Read data from the packet
		// MUST be read in the same order it was written
		template <typename T> auto ReadData() const
		{
			T Temp;
			BinaryOut->operator()(Temp);
			return Temp;
		}

		// Get the current, raw serialized, data from the packet
		const auto GetData() const			{ return DataStream.rdbuf()->str(); }
		// Get the current, raw serialized, data size from the packet
		const auto GetDataSize() const		{ return DataStream.rdbuf()->str().size(); }
		// Get the packets type
		const auto GetType() const			{ return TypeID; }
		// Get the packets ID
		const auto GetPacketID() const		{ return PacketID; }
		//	Get the creation time
		const auto GetCreationTime() const	{ return CreationTime; }

		// Returns true if packet needs resend
		// Waits 900ms between send attempts
		const auto NeedsResend() {
			if (SendAttempts > 5) { return false; }	//	Maximum sends reached; return false
			
			auto Now = std::chrono::high_resolution_clock::now();	//	This a performance heavy function; call it only when needed.
			if (Now < NextSendTime) { return false; }		//	Not enough time has elapsed since previous send attempt; return false

			NextSendTime = Now + std::chrono::milliseconds(900);
			++SendAttempts;
			return true;
		}

		//	Reliable delivery failed
		const auto NeedsDelete() const { return (SendAttempts >= 5); }

		//	Return our underlying destination NetPeer
		auto GetPeer() const { return MyPeer; }

	};

}