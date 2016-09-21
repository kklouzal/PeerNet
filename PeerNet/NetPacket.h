#pragma once

namespace PeerNet
{
	class NetPacket
	{
		std::chrono::time_point<std::chrono::system_clock> CreationTime;

		unsigned int PacketID;
		unsigned short TypeID;
		std::stringstream DataStream;
		cereal::PortableBinaryOutputArchive BinaryIn;
		cereal::PortableBinaryInputArchive BinaryOut;

	public:
		// Receive Packet
		NetPacket(const std::string Data) : DataStream(Data, std::ios::in | std::ios::out | std::ios::binary), BinaryIn(DataStream), BinaryOut(DataStream)//, CreationTime(std::chrono::system_clock::now())
		{
			BinaryOut(PacketID);
			BinaryOut(TypeID);
			if (TypeID == PacketType::PN_ACK) { CreationTime = std::chrono::system_clock::now(); }
		}

		// Send Packet
		NetPacket(const unsigned long pID, const unsigned short pType) : PacketID(pID), TypeID(pType), DataStream(std::ios::in | std::ios::out | std::ios::binary), BinaryIn(DataStream), BinaryOut(DataStream)//, CreationTime(std::chrono::system_clock::now())
		{
			if (IsReliable()) { CreationTime = std::chrono::system_clock::now(); }
			BinaryIn(PacketID);
			BinaryIn(TypeID);
		}

		template <class T> void WriteData(T Data) {	BinaryIn(Data);	}

		template <class T> T ReadData()
		{
			T Temp;
			BinaryOut(Temp);
			return Temp;
		}

		void RecvAck(NetPacket*const Packet)
		{
			printf("Reliable ACK# %i %.3fms\n", PacketID, (std::chrono::duration<double>(Packet->CreationTime - CreationTime).count() * 1000));
			TypeID = PacketType::PN_DELETE;
		}

		const std::string GetData() const {	return DataStream.rdbuf()->str(); }

		const PacketType GetType() const { return (PacketType)TypeID; }

		const unsigned long GetPacketID() const	{ return PacketID; }

		const bool IsReliable() const {	return ((TypeID == PacketType::PN_Discovery) || (TypeID == PacketType::PN_Reliable)); }
	};

}