#pragma once

namespace PeerNet
{
	class NetPacket
	{
		std::chrono::time_point<std::chrono::high_resolution_clock> CreationTime;

		unsigned int PacketID;
		unsigned short TypeID;
		std::stringstream DataStream;
		cereal::PortableBinaryOutputArchive BinaryIn;
		cereal::PortableBinaryInputArchive BinaryOut;

	public:
		unsigned short SendAttempts;

		// This constructor is for handling Receive Packets ONLY
		NetPacket(const std::string Data) : DataStream(Data, std::ios::in | std::ios::out | std::ios::binary), BinaryIn(DataStream), BinaryOut(DataStream), SendAttempts(0)
		{
			BinaryOut(PacketID);
			BinaryOut(TypeID);
			if (TypeID == PacketType::PN_ACK) { CreationTime = std::chrono::high_resolution_clock::now(); }
		}

		// This constructor is for handling Send Packets ONLY
		NetPacket(const unsigned long pID, const unsigned short pType) : PacketID(pID), TypeID(pType), DataStream(std::ios::in | std::ios::out | std::ios::binary), BinaryIn(DataStream), BinaryOut(DataStream), SendAttempts(0)
		{
			if (IsReliable()) { CreationTime = std::chrono::high_resolution_clock::now(); }
			BinaryIn(PacketID);
			BinaryIn(TypeID);
		}

		// Write data into the packet
		template <class T> void WriteData(T Data) {	BinaryIn(Data);	}

		// Read data from the packet
		// MUST be read in the same order it was written
		template <class T> T ReadData()
		{
			T Temp;
			BinaryOut(Temp);
			return Temp;
		}

		void RecvAck(NetPacket*const Packet)
		{
			printf("Reliable ACK# %i %.3fms\n", PacketID, (std::chrono::duration<double>(Packet->CreationTime - CreationTime).count() * 1000));
		}

		// Get the current, raw serialized, data from the packet
		const std::string GetData() const {	return DataStream.rdbuf()->str(); }

		// Get the packets type
		const PacketType GetType() const { return (PacketType)TypeID; }

		// Get the packets ID
		const unsigned long GetPacketID() const	{ return PacketID; }

		// Is this a reliable packet
		const bool IsReliable() const {	return ((TypeID == PacketType::PN_Discovery) || (TypeID == PacketType::PN_Reliable)); }

		// Returns true if packet needs resend
		// Waits 300ms between send attempts
		const bool NeedsResend() const { return (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - CreationTime).count() > (0.3*SendAttempts)); }
	};

}