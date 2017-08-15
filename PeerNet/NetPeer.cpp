#include "PeerNet.h"

namespace PeerNet
{
	NetPeer::NetPeer(NetSocket*const DefaultSocket, NetAddress*const NetAddr)
		: Address(NetAddr), Socket(DefaultSocket), RollingRTT(6), Avg_RTT(300),
		CH_KOL(new KeepAliveChannel(PN_KeepAlive)),
		CH_Ordered(new OrderedChannel(PN_Ordered)),
		CH_Reliable(new ReliableChannel(PN_Reliable)),
		CH_Unreliable(new UnreliableChannel(PN_Unreliable)),
		TimedEvent(std::chrono::milliseconds(300), 0)	//	Start with value of Avg_RTT
	{
		//	Start the Keep-Alive sequence which will initiate the connection
		this->StartTimer();
		Log("\tConnect Peer - " + string(Address->FormattedAddress()) + "\n");
	}

	NetPeer::~NetPeer()
	{
		this->StopTimer();
		delete CH_KOL;
		delete CH_Ordered;
		delete CH_Reliable;
		delete CH_Unreliable;
		Log("\tDisconnect Peer - " + string(Address->FormattedAddress()) + "\n");
	}

	//	BaseClass TimedEvent OnTick function
	//	Used for Keep-Alive and ACK sync
	void NetPeer::OnTick()
	{
		//	Keep a rolling average of the last 6 values returned by CH_KOL->RTT()
		//	This spreads our RTT up to about 30 seconds for a 250ms ping
		//	And about 6 seconds for a 50ms ping
		Avg_RTT -= Avg_RTT / RollingRTT;
		Avg_RTT += CH_KOL->RTT() / RollingRTT;

		//	Update our timed interval based on past Keep Alive RTT's
		NewInterval(Avg_RTT);
		//	Keep-Alive Protocol:
		//
		//	(bool)				Is this not an Acknowledgement?
		//	(unsigned long)		Highest Received KOL Packet ID
		//	(unsigned long)		Highest Received Reliable Packet ID
		//	(unsigned long)		Highest Received Unreliable Packet ID
		//	(unsigned long)		Highest Received && Processed Ordered Packet ID
		//	(unordered_map)*					Missing Ordered Reliable Packet ID's
		//	(std::chrono::milliseconds)*		My Reliable RTT
		//	(std::chrono::milliseconds)	*		My Reliable Ordered RTT
		//
		auto KeepAlive = CreateNewPacket(PacketType::PN_KeepAlive);
		KeepAlive->WriteData<bool>(true);
		KeepAlive->WriteData<unsigned long>(CH_KOL->GetLastID());
		KeepAlive->WriteData<unsigned long>(CH_Reliable->GetLastID());
		KeepAlive->WriteData<unsigned long>(CH_Unreliable->GetLastID());
		KeepAlive->WriteData<unsigned long>(CH_Ordered->GetLastID());
		Send_Packet(KeepAlive.get());
	}

	//	BaseClass TimedEvent OnExpire function
	//	This should never call as a clients timed event infinitely ticks until the client is destroyed
	void NetPeer::OnExpire()
	{
		Log("\tClient Tick Expire\n");
	}

	//	Send a packet
	//	External usage only and as a means to introduce a packet into a socket for transmission
	void NetPeer::Send_Packet(SendPacket* Packet) {
		Socket->PostCompletion<SendPacket*>(CK_SEND, Packet);
	}
	//
	//	Called from a NetSocket's Send function
	//	OUT_Packet = Outgoing packet being compressed
	//	DataBuffer = Preallocated buffer to store our compressed data
	//	MaxDataSize = Maximum allowed size after compression
	//	Return Value - Any integer greater than 0 for success
	const size_t NetPeer::CompressPacket(SendPacket*const OUT_Packet, PCHAR DataBuffer, const size_t MaxDataSize, ZSTD_CCtx* CCtx)
	{
		//return LZ4_compress_default(OUT_Packet->GetData()->str().c_str(), DataBuffer, (int)OUT_Packet->GetData()->str().size(), MaxDataSize);
		return ZSTD_compressCCtx(CCtx, DataBuffer, MaxDataSize, OUT_Packet->GetData()->str().c_str(), OUT_Packet->GetData()->str().size(), 1);
	}
	//
	//	Called from a NetSocket's Receive function
	//	TypeID = Type of incoming packet
	//	IncomingData = Raw incoming data payload
	//	DataSize = IncomingData size
	//	MaxDataSize = Maximum allowed size after decompression
	//	CompressionBuffer = Preallocated buffer for use during decompression
	void NetPeer::Receive_Packet(u_short TypeID, const PCHAR IncomingData, const size_t DataSize, const size_t MaxDataSize, char*const CBuff, ZSTD_DCtx* DCtx )
	{
		//	Disreguard any incoming packets for this peer if our Keep-Alive sequence isnt active
		if (!TimerRunning()) { return; }

		//	Decompress the incoming data payload
		//const int DecompressResult = LZ4_decompress_safe(IncomingData, CompressionBuffer, DataSize, MaxDataSize);
		const size_t DecompressResult = ZSTD_decompressDCtx(DCtx, CBuff, MaxDataSize, IncomingData, DataSize);

		//	Return if decompression fails
		if (DecompressResult < 0) { Log("Receive Packet - Decompression Failed!\n"); return; }

		//	Instantiate a NetPacket from our decompressed data
		ReceivePacket*const IncomingPacket = new ReceivePacket(std::string(CBuff, DecompressResult));

		//	Process the packet as needed
		switch (IncomingPacket->GetType()) {

		case PN_KeepAlive:
			if (CH_KOL->Receive(IncomingPacket))
			{
				//	Process this Keep-Alive Packet
				//	Memory for the ACK is cleaned up by the NetSocket that sends it
				SendPacket* ACK = new SendPacket(IncomingPacket->GetPacketID(), PN_KeepAlive, true);
				ACK->SetDestination(this);
				ACK->WriteData<bool>(false);
				Send_Packet(ACK);

				CH_KOL->ACK(IncomingPacket->ReadData<unsigned long>());
				CH_Reliable->ACK(IncomingPacket->ReadData<unsigned long>());
				CH_Unreliable->ACK(IncomingPacket->ReadData<unsigned long>());
				CH_Ordered->ACK(IncomingPacket->ReadData<unsigned long>());

				//	End Keep-Alive Packet Processing
				delete IncomingPacket;
			}
		break;

		case PN_Unreliable:
			if (CH_Unreliable->Receive(IncomingPacket))
			{
				//	Call packet's callback function?
				Log("Unreliable - " + std::to_string(IncomingPacket->GetPacketID()) + " - " + IncomingPacket->ReadData<std::string>() + "\n");
				//	For now just delete the IncomingPacket
				delete IncomingPacket;
			}
		break;

		case PN_Reliable:
			if (CH_Reliable->Receive(IncomingPacket))
			{
				//	Call packet's callback function?
				Log("Reliable - " + std::to_string(IncomingPacket->GetPacketID()) + " - " + IncomingPacket->ReadData<std::string>() + "\n");
				//	For now just delete the IncomingPacket
				delete IncomingPacket;
			}
		break;

		//	Ordered packeds need processed inside their Receive function
		case PN_Ordered:	CH_Ordered->Receive(IncomingPacket); break;

		//	Default case for unknown packet type
		default: Log("Recv Unknown Packet Type\n"); delete IncomingPacket;
		}
	}
}