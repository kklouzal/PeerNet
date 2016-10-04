#pragma once

namespace PeerNet
{
	class NetPacket
	{
		NetSocket* const MySocket;	//	Socket we'll use for communication
		NetPeer* const MyPeer;		//	The destination peer for this packet

		std::chrono::time_point<std::chrono::high_resolution_clock> CreationTime;
		std::chrono::time_point<std::chrono::high_resolution_clock> NextSendTime;

		unsigned int PacketID;
		unsigned short TypeID;
		std::stringstream DataStream;
		cereal::PortableBinaryOutputArchive* BinaryIn;
		cereal::PortableBinaryInputArchive* BinaryOut;

	public:
		unsigned short SendAttempts;

		// This constructor is for handling Receive Packets ONLY
		NetPacket(const std::string Data)
			: DataStream(Data, std::ios::in | std::ios::out | std::ios::binary), BinaryIn(nullptr),
			BinaryOut(new cereal::PortableBinaryInputArchive(DataStream)), SendAttempts(0), MySocket(nullptr), MyPeer(nullptr)
		{
			BinaryOut->operator()(PacketID);
			BinaryOut->operator()(TypeID);
			if (TypeID == PacketType::PN_ACK) { CreationTime = std::chrono::high_resolution_clock::now(); }
		}

		// This constructor is for handling Send Packets ONLY
		NetPacket(const unsigned long pID, const unsigned short pType, NetSocket* const Socket, NetPeer* const Peer)
			: PacketID(pID), TypeID(pType), DataStream(std::ios::in | std::ios::out | std::ios::binary), BinaryIn(new cereal::PortableBinaryOutputArchive(DataStream)),
			BinaryOut(nullptr), SendAttempts(0), MySocket(Socket), MyPeer(Peer)
		{
			if (IsReliable()) {
				CreationTime = std::chrono::high_resolution_clock::now();
				NextSendTime = CreationTime + std::chrono::milliseconds(300);
			}
			BinaryIn->operator()(PacketID);
			BinaryIn->operator()(TypeID);
		}

		//	Default Destructor
		~NetPacket()
		{
			if (BinaryIn != nullptr) { delete BinaryIn; }
			if (BinaryOut != nullptr) { delete BinaryOut; }
		}

		// Write data into the packet
		template <class T> void WriteData(T Data) {	BinaryIn->operator()(Data);	}

		// Read data from the packet
		// MUST be read in the same order it was written
		template <class T> T ReadData()
		{
			T Temp;
			BinaryOut->operator()(Temp);
			return Temp;
		}

		// Get the current, raw serialized, data from the packet
		const std::string GetData() const {	return DataStream.rdbuf()->str(); }

		// Get the packets type
		const PacketType GetType() const { return (PacketType)TypeID; }

		// Get the packets ID
		const unsigned long GetPacketID() const { return PacketID; }

		//	Get the creation time
		const std::chrono::time_point<std::chrono::high_resolution_clock> GetCreationTime() const { return CreationTime; }

		// Is this a reliable packet
		const bool IsReliable() const {	return ((TypeID == PacketType::PN_Discovery) || (TypeID == PacketType::PN_Reliable)); }

		// Returns true if packet needs resend
		// Waits 300ms between send attempts
		const bool NeedsResend() {
			if (std::chrono::high_resolution_clock::now() > NextSendTime)
			{
				NextSendTime += std::chrono::milliseconds(350);
				return true;
			}
			return false;
		}

		//	Send your finialized packet
		//	Do not ever touch the packet again after calling this
		void Send()
		{
			MySocket->AddOutgoingPacket(MyPeer, this);
		}

		//	Return our underlying destination NetPeer
		NetPeer*const GetPeer() const { return MyPeer; }

		//	A pathetic attempt at some crude statistics
		void Acknowledge(std::chrono::time_point<std::chrono::high_resolution_clock> Ack)
		{
			printf("Reliable ACK# %i %.3fms\n", PacketID, (std::chrono::duration<double>(Ack - CreationTime).count() * 1000));
		}

	};

}