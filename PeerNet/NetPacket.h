#pragma once
#include "TimedEvent.hpp"

namespace PeerNet
{
	using cereal::PortableBinaryOutputArchive;
	using cereal::PortableBinaryInputArchive;
	using std::chrono::high_resolution_clock;
	using std::stringstream;
	using std::string;

	class NetPacket : public OVERLAPPED
	{
	protected:
		high_resolution_clock::time_point CreationTime;
		unsigned long PacketID = 0;
		PacketType TypeID = PN_NotInialized;

	public:
		//	Get the creation time
		const auto GetCreationTime() const	{ return CreationTime; }
		// Get the packets ID
		const auto GetPacketID() const		{ return PacketID; }
		// Get the packets type
		const auto GetType() const			{ return TypeID; }
	};

	//
	//	Specialized SendPacket
	class SendPacket : public NetPacket
	{
		stringstream DataStream;					//	DataStream holds our serialized binary data
		bool InternallyManaged;						//	If the data held by MyNetPacket is deleted or not
		NetPeer* MyPeer;							//	The destination peer for this SendPacket
		PortableBinaryOutputArchive*const BinaryIn;	//	Putting binary into the archive to send out

	public:
		//	Managed == true ONLY for non-user accessible packets
		SendPacket(const unsigned long pID, const PacketType pType, const bool Managed = false)
			: DataStream(std::ios::in | std::ios::out | std::ios::binary), InternallyManaged(Managed),
			BinaryIn(new PortableBinaryOutputArchive(DataStream))
		{
			PacketID = pID;
			TypeID = pType;
			BinaryIn->operator()(pID);
			BinaryIn->operator()(pType);
			if (pType == PN_KeepAlive) { CreationTime = high_resolution_clock::now(); }
		}

		~SendPacket() { delete BinaryIn; }

		// Set destination peer
		void SetDestination(NetPeer* DestPeer) { MyPeer = DestPeer; }

		// Write data into the packet
		template <typename T> void WriteData(T Data) const { BinaryIn->operator()(Data); }
		// Get the packets data buffer
		const auto GetData() const { return DataStream.rdbuf(); }
		//	Return our underlying destination NetPeer
		auto const GetPeer() const { return MyPeer; }
		auto const GetManaged() const { return InternallyManaged; }
	};

	//
	//	Specialized ReceivePacket
	class ReceivePacket : public NetPacket
	{
		stringstream DataStream;					//	DataStream holds our serialized binary data
		PortableBinaryInputArchive*const BinaryOut;	//	Pulling binary out of the archive we received

	public:
		//	Managed == true ONLY for non-user accessible packets
		ReceivePacket(const string Data)
			: DataStream(Data, std::ios::in | std::ios::out | std::ios::binary),
			BinaryOut(new PortableBinaryInputArchive(DataStream))
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
		// Get the packets data buffer
		const auto GetData() const { return DataStream.rdbuf(); }
	};
}