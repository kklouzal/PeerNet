#pragma once
#include "TimedEvent.hpp"
#include "Channel.hpp"

namespace PeerNet
{
	class NetPeer : public TimedEvent
	{
		NetAddress*const Address;

		const long long RollingRTT;			//	Keep a rolling average of the last estimated 6 Round Trip Times
											//	- That should equate to about 30 seconds worth of averaging with a 250ms average RTT
		duration<double, milli> Avg_RTT;	//	Start the system off assuming a 300ms ping. Let the algorythms adjust from that point.

		KeepAliveChannel* CH_KOL;
		OrderedChannel* CH_Ordered;
		ReliableChannel* CH_Reliable;
		UnreliableChannel* CH_Unreliable;

		void OnTick();
		void OnExpire();

	public:
		NetSocket*const Socket;

		NetPeer(NetSocket*const DefaultSocket, NetAddress*const NetAddr);

		//NetPeer(const std::string StrIP, const std::string StrPort, NetSocket*const DefaultSocket);
		~NetPeer();

		//	Construct and return a NetPacket to fill and send to this NetPeer
		inline auto NetPeer::CreateNewPacket(const PacketType pType) {
			if (pType == PN_KeepAlive)
			{
				return CH_KOL->NewPacket();
			}
			else if (pType == PN_Ordered)
			{
				return CH_Ordered->NewPacket();
			}
			else if (pType == PN_Reliable)
			{
				return CH_Reliable->NewPacket();
			}

			return CH_Unreliable->NewPacket();
		}

		inline void Receive_Packet(const PCHAR IncomingData, const size_t& DataSize, const size_t& MaxDataSize, char*const CBuff, ZSTD_DCtx*const DCtx)
		{
			//	Disreguard any incoming packets for this peer if our Keep-Alive sequence isnt active
			if (!TimerRunning()) { return; }

			//	Decompress the incoming data payload
			//const int DecompressResult = LZ4_decompress_safe(IncomingData, CompressionBuffer, DataSize, MaxDataSize);
			const size_t DecompressResult = ZSTD_decompressDCtx(DCtx, CBuff, MaxDataSize, IncomingData, DataSize);

			//	Return if decompression fails
			//	TODO: Should be < 0; Will randomly crash at 0 though.
			if (DecompressResult < 1) {
				printf("Receive Packet - Decompression Failed!\n"); return;
			}

			//	Instantiate a NetPacket from our decompressed data
			ReceivePacket*const IncomingPacket = new ReceivePacket(std::string(CBuff, DecompressResult));

			//	Process the packet as needed
			switch (IncomingPacket->GetType()) {

			case PN_KeepAlive:
				if (CH_KOL->Receive(IncomingPacket))
				{
					//	Process this Keep-Alive Packet
					//	Memory for the ACK is cleaned up by the NetSocket that sends it
					SendPacket*const ACK = new SendPacket(IncomingPacket->GetPacketID(), PN_KeepAlive, this, true);
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
					printf("Unreliable - %d - %s\n", IncomingPacket->GetPacketID(), IncomingPacket->ReadData<std::string>().c_str());
					//	For now just delete the IncomingPacket
					delete IncomingPacket;
				}
				break;

			case PN_Reliable:
				if (CH_Reliable->Receive(IncomingPacket))
				{
					//	Call packet's callback function?
					printf("Reliable - %d - %s\n", IncomingPacket->GetPacketID(), IncomingPacket->ReadData<std::string>().c_str());
					//	For now just delete the IncomingPacket
					delete IncomingPacket;
				}
				break;

				//	Ordered packeds need processed inside their Receive function
			case PN_Ordered:	CH_Ordered->Receive(IncomingPacket); break;

				//	Default case for unknown packet type
			default: printf("Recv Unknown Packet Type\n"); delete IncomingPacket;
			}
		}
		void Send_Packet(SendPacket*const Packet);

		inline const size_t CompressPacket(SendPacket*const OUT_Packet, PCHAR DataBuffer, const size_t& MaxDataSize, ZSTD_CCtx*const CCtx)
		{
			//return LZ4_compress_default(OUT_Packet->GetData()->str().c_str(), DataBuffer, (int)OUT_Packet->GetData()->str().size(), MaxDataSize);
			return ZSTD_compressCCtx(CCtx, DataBuffer, MaxDataSize, OUT_Packet->GetData()->str().c_str(), OUT_Packet->GetData()->str().size(), 1);
		}

		inline const auto RTT_KOL() const { return Avg_RTT; }

		inline NetAddress*const GetAddress() const { return Address; }
	};
}