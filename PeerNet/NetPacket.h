#pragma once
#include "TimedEvent.hpp"

namespace PeerNet
{
	class NetPacket
	{
		NetPeer*const MyPeer;		//	The destination peer for this packet

		std::chrono::time_point<std::chrono::high_resolution_clock> CreationTime;

		unsigned long PacketID;
		PeerNet::PacketType TypeID;

		std::stringstream DataStream;
		cereal::PortableBinaryOutputArchive*const BinaryIn;
		cereal::PortableBinaryInputArchive*const BinaryOut;

		//	Is the memory for this packet handled internally or through a shared_ptr
		bool InternallyManaged;		//	Only true for non-user accessible packets

	public:
		//	This constructor is for handling Receive Packets ONLY
		//	Managed == true ONLY for non-user accessible packets
		NetPacket(const std::string Data, const bool Managed = false)
			: DataStream(Data, std::ios::in | std::ios::out | std::ios::binary),
			BinaryIn(nullptr), BinaryOut(new cereal::PortableBinaryInputArchive(DataStream)),
			MyPeer(nullptr), InternallyManaged(Managed)
		{
			BinaryOut->operator()(PacketID);
			BinaryOut->operator()(TypeID);
			if (TypeID == PacketType::PN_KeepAlive) { CreationTime = std::chrono::high_resolution_clock::now(); }
		}

		//	This constructor is for handling Send Packets ONLY
		//	Managed == true ONLY for non-user accessible packets
		NetPacket(const unsigned long pID, const PeerNet::PacketType pType, NetPeer*const Peer, const bool Managed = false)
			: DataStream(std::ios::in | std::ios::out | std::ios::binary),
			BinaryIn(new cereal::PortableBinaryOutputArchive(DataStream)), BinaryOut(nullptr),
			MyPeer(Peer), InternallyManaged(Managed), PacketID(pID), TypeID(pType)
		{
			BinaryIn->operator()(PacketID);
			BinaryIn->operator()(TypeID);
			if (TypeID == PacketType::PN_KeepAlive) { CreationTime = std::chrono::high_resolution_clock::now(); }
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
		//	Return our underlying destination NetPeer
		auto const GetPeer() const			{ return MyPeer; }

		auto const GetManaged() const		{ return InternallyManaged; }
	};
}