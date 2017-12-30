#pragma once
#include "TimedEvent.hpp"
#include <atomic>

namespace PeerNet
{
	using cereal::PortableBinaryOutputArchive;
	using cereal::PortableBinaryInputArchive;
	using std::chrono::steady_clock;
	using std::stringstream;
	using std::string;

	//
	//	Specialized SendPacket
	class SendPacket : public OVERLAPPED
	{
		stringstream DataStream;				//	DataStream holds our serialized binary data
		PortableBinaryOutputArchive BinaryIn;	//	Putting binary into the archive to send out

		const unsigned long PacketID;
		const PacketType TypeID;
		const unsigned long OperationID;

		const bool InternallyManaged;					//	If the data held by MyNetPacket is automatically deleted or not
		const steady_clock::time_point CreationTime;	//	The creation time for this packet used for RTT calculations
		//
		NetAddress*const MyAddress;

	public:
		//	IsSending flag = true to stop ACK cleanups
		std::atomic<unsigned char> IsSending;
		std::atomic<unsigned char> NeedsDelete;

		//	Managed == true ONLY for non-user accessible packets
		inline SendPacket(const unsigned long pID, const PacketType pType, const unsigned long OpID, NetAddress*const Address, const bool Managed = false, steady_clock::time_point CT = steady_clock::now())
			: DataStream(std::ios::in | std::ios::out | std::ios::binary), BinaryIn(DataStream),
			PacketID(pID), TypeID(pType), OperationID(OpID),
			InternallyManaged(Managed), CreationTime(CT),
			MyAddress(Address), IsSending(1), NeedsDelete(0)
		{
			BinaryIn(pID);
			BinaryIn(pType);
			BinaryIn(OpID);
			BinaryIn(CreationTime);
		}

		inline ~SendPacket() {}

		// Write data into the packet
		// MUST be read in the same order it was written
		template <typename T> inline void WriteData(T Data) { BinaryIn(Data); }
		// Get the packets data buffer
		inline const auto GetData() const { return DataStream.rdbuf(); }
		//	Return our underlying destination NetPeer
		inline auto GetAddress() const { return MyAddress; }
		//	Is this an internally managed packet
		inline const auto& GetManaged() const { return InternallyManaged; }
		//	Get the creation time
		inline const auto& GetCreationTime() const { return CreationTime; }
		// Get the packets ID
		inline const auto& GetPacketID() const { return PacketID; }
		// Get the packets Operation ID
		inline const auto& GetOperationID() const { return OperationID; }
		// Get the packets type
		inline const auto& GetType() const { return TypeID; }
	};

	//
	//	Specialized ReceivePacket
	class ReceivePacket
	{
		stringstream DataStream;				//	DataStream holds our serialized binary data
		PortableBinaryInputArchive BinaryOut;	//	Pulling binary out of the archive we received

		steady_clock::time_point CreationTime;
		unsigned long PacketID = 0;
		PacketType TypeID = PN_NotInialized;
		unsigned long OperationID = 0;

	public:
		//	Managed == true ONLY for non-user accessible packets
		inline ReceivePacket(const string Data)
			: DataStream(Data, std::ios::in | std::ios::out | std::ios::binary),
			BinaryOut(DataStream)
		{
			BinaryOut(PacketID);
			BinaryOut(TypeID);
			BinaryOut(OperationID);
			BinaryOut(CreationTime);
		}

		inline ~ReceivePacket() {}

		// Read data from the packet
		// MUST be read in the same order it was written
		template <typename T> inline auto ReadData()
		{
			T Temp;
			BinaryOut(Temp);
			return Temp;
		}
		// Get the packets data buffer
		inline const auto GetData() const { return DataStream.rdbuf(); }
		//	Get the creation time
		inline const auto& GetCreationTime() const { return CreationTime; }
		// Get the packets ID
		inline const auto& GetPacketID() const { return PacketID; }
		// Get the packets Operation ID
		inline const auto& GetOperationID() const { return OperationID; }
		// Get the packets type
		inline const auto& GetType() const { return TypeID; }
	};
}