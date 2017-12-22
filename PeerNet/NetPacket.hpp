#pragma once
#include "TimedEvent.hpp"
#include <atomic>

namespace PeerNet
{
	using cereal::PortableBinaryOutputArchive;
	using cereal::PortableBinaryInputArchive;
	using std::chrono::high_resolution_clock;
	using std::stringstream;
	using std::string;

	//
	//	Base NetPacket
	class NetPacket : public OVERLAPPED
	{
	protected:
		high_resolution_clock::time_point CreationTime;
		unsigned long PacketID = 0;
		PacketType TypeID = PN_NotInialized;

	public:
		std::atomic<bool> IsSending = true;
		//	Get the creation time
		inline const auto& GetCreationTime() const	{ return CreationTime; }
		// Get the packets ID
		inline const auto& GetPacketID() const		{ return PacketID; }
		// Get the packets type
		inline const auto& GetType() const			{ return TypeID; }
	};

	//
	//	Specialized SendPacket
	class SendPacket : public NetPacket
	{
		stringstream DataStream;					//	DataStream holds our serialized binary data
		const bool InternallyManaged;				//	If the data held by MyNetPacket is deleted or not
		//NetPeer*const MyPeer;						//	The destination peer for this SendPacket
		const NetAddress*const MyAddress;
		PortableBinaryOutputArchive*const BinaryIn;	//	Putting binary into the archive to send out

	public:
		//	Managed == true ONLY for non-user accessible packets
		inline SendPacket(const unsigned long& pID, const PacketType& pType, const NetAddress*const Address, const bool& Managed = false)
			: DataStream(std::ios::in | std::ios::out | std::ios::binary), InternallyManaged(Managed), MyAddress(Address),
			BinaryIn(new PortableBinaryOutputArchive(DataStream))
		{
			PacketID = pID;
			TypeID = pType;
			BinaryIn->operator()(pID);
			BinaryIn->operator()(pType);
			if (pType == PN_KeepAlive) { CreationTime = high_resolution_clock::now(); }
		}

		inline ~SendPacket() { delete BinaryIn; }

		// Write data into the packet
		template <typename T> inline void WriteData(T Data) const { BinaryIn->operator()(Data); }
		// Get the packets data buffer
		inline const auto GetData() const { return DataStream.rdbuf(); }
		//	Return our underlying destination NetPeer
		inline auto const GetAddress() const { return MyAddress; }
		inline const auto& GetManaged() const { return InternallyManaged; }
	};

	//
	//	Specialized ReceivePacket
	class ReceivePacket : public NetPacket
	{
		stringstream DataStream;					//	DataStream holds our serialized binary data
		PortableBinaryInputArchive*const BinaryOut;	//	Pulling binary out of the archive we received

	public:
		//	Managed == true ONLY for non-user accessible packets
		inline ReceivePacket(const string& Data)
			: DataStream(Data, std::ios::in | std::ios::out | std::ios::binary),
			BinaryOut(new PortableBinaryInputArchive(DataStream))
		{
			BinaryOut->operator()(PacketID);
			BinaryOut->operator()(TypeID);
			if (TypeID == PN_KeepAlive) { CreationTime = high_resolution_clock::now(); }
		}

		inline ~ReceivePacket() { delete BinaryOut; }

		// Read data from the packet
		// MUST be read in the same order it was written
		template <typename T> inline auto ReadData() const
		{
			T Temp;
			BinaryOut->operator()(Temp);
			return Temp;
		}
		// Get the packets data buffer
		inline const auto GetData() const { return DataStream.rdbuf(); }
	};
}