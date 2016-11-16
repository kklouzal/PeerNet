#pragma once
#include "TimedEvent.hpp"

namespace PeerNet
{
	using cereal::PortableBinaryOutputArchive;
	using cereal::PortableBinaryInputArchive;
	using std::chrono::high_resolution_clock;
	using std::chrono::time_point;
	using std::stringstream;
	using std::string;

	class NetPacket
	{
	protected:
		time_point<high_resolution_clock> CreationTime;

		unsigned long PacketID;
		PacketType TypeID;

		//	Is the memory for this packet handled internally or through a shared_ptr
		bool InternallyManaged;		//	Only true for non-user accessible packets

		//	DataStream holds our serialized binary data
		stringstream DataStream;

		//	Managed == true ONLY for non-user accessible packets
		NetPacket(const bool Managed, const string Data = string()) : InternallyManaged(Managed), DataStream(Data, std::ios::in | std::ios::out | std::ios::binary) {}

	public:
		//	Default Destructor
		~NetPacket() {}

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

		auto const GetManaged() const		{ return InternallyManaged; }
	};

	//
	//	Specialized SendPacket
	class SendPacket : public NetPacket
	{
		NetPeer*const MyPeer;		//	The destination peer for this SendPacket

		PortableBinaryOutputArchive* BinaryIn;	//	Putting binary into the archive to send out

	public:
		//	Managed == true ONLY for non-user accessible packets
		SendPacket(const unsigned long pID, const PacketType pType, NetPeer*const Peer, const bool Managed = false)
			: MyPeer(Peer), NetPacket(Managed), BinaryIn(new PortableBinaryOutputArchive(DataStream))
		{
			PacketID = pID;
			TypeID = pType;
			BinaryIn->operator()(PacketID);
			BinaryIn->operator()(TypeID);
			if (TypeID == PN_KeepAlive) { CreationTime = high_resolution_clock::now(); }
		}

		~SendPacket() { delete BinaryIn; }

		// Write data into the packet
		template <typename T> void WriteData(T Data) const { BinaryIn->operator()(Data); }
		//	Return our underlying destination NetPeer
		auto const GetPeer() const { return MyPeer; }
	};

	//
	//	Specialized ReceivePacket
	class ReceivePacket : public NetPacket
	{
		PortableBinaryInputArchive* BinaryOut;	//	Pulling binary out of the archive we received

	public:
		//	Managed == true ONLY for non-user accessible packets
		ReceivePacket(const string Data, const bool Managed = false)
			: NetPacket(Managed, Data), BinaryOut(new PortableBinaryInputArchive(DataStream))
		{
			BinaryOut->operator()(PacketID);
			BinaryOut->operator()(TypeID);
			if (TypeID == PN_KeepAlive) { CreationTime = high_resolution_clock::now(); }
		}

		~ReceivePacket() { delete BinaryOut; }

		// Read data from the packet
		// MUST be read in the same order it was written
		template <typename T> auto ReadData() const
		{
			T Temp;
			BinaryOut->operator()(Temp);
			return Temp;
		}
	};
}