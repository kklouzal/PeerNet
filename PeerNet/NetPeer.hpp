#pragma once
#include "TimedEvent.hpp"
#include "Channel.hpp"

namespace PeerNet
{
	class NetPeer : public TimedEvent
	{
		PeerNet* _PeerNet = nullptr;

		NetAddress*const Address;

		const long long RollingRTT;			//	Keep a rolling average of the last estimated 6 Round Trip Times
											//	- That should equate to about 30 seconds worth of averaging with a 250ms average RTT
		duration<double, std::milli> Avg_RTT;	//	Start the system off assuming a 300ms ping. Let the algorythms adjust from that point.

		KeepAliveChannel* CH_KOL;
		OrderedChannel* CH_Ordered;
		ReliableChannel* CH_Reliable;
		UnreliableChannel* CH_Unreliable;

		inline void OnTick()
		{
			//	Check to see if this peer is no longer alive
			if (CH_KOL->GetUnacknowledgedCount() > 100) {
				_PeerNet->DisconnectPeer(this);
			} else {
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
		}
		inline void NetPeer::OnExpire()
		{
			printf("\tClient Tick Expire\n");
		}

	public:
		NetSocket*const Socket;

		//	Constructor
		inline NetPeer(PeerNet* PNInstance, NetSocket*const DefaultSocket, NetAddress*const NetAddr)
			: _PeerNet(PNInstance), Address(NetAddr), Socket(DefaultSocket), RollingRTT(6), Avg_RTT(300),
			CH_KOL(new KeepAliveChannel(Address, PN_KeepAlive)),
			CH_Ordered(new OrderedChannel(Address, PN_Ordered)),
			CH_Reliable(new ReliableChannel(Address, PN_Reliable)),
			CH_Unreliable(new UnreliableChannel(Address, PN_Unreliable)),
			TimedEvent(std::chrono::milliseconds(100), 0)	//	Start with value of Avg_RTT
		{
			//	Start the Keep-Alive sequence which will initiate the connection
			this->StartTimer();
			printf("\tConnect Peer - %s\n", Address->FormattedAddress());
		}

		//	Destructor
		inline ~NetPeer()
		{
			this->StopTimer();
			delete CH_KOL;
			delete CH_Ordered;
			delete CH_Reliable;
			delete CH_Unreliable;
			printf("\tDisconnect Peer - %s\n", Address->FormattedAddress());
			//	Cleanup our NetAddress
			//	TODO: Need to return this address back into the Unused Address Pool instead of deleting it
			delete Address;
		}

		//	Construct and return a NetPacket to fill and send to this NetPeer
		inline shared_ptr<SendPacket> NetPeer::CreateNewPacket(const PacketType pType) {
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

		inline void Receive_Packet(const string& IncomingData)
		{
			//	Disreguard any incoming packets for this peer if our Keep-Alive sequence isnt active
			if (!TimerRunning()) { return; }

			//	Instantiate a NetPacket from our decompressed data
			ReceivePacket*const IncomingPacket = new ReceivePacket(IncomingData);

			//	Process the packet as needed
			switch (IncomingPacket->GetType()) {

			case PN_KeepAlive:
				if (CH_KOL->Receive(IncomingPacket))
				{
					//	Process this Keep-Alive Packet
					//	Memory for the ACK is cleaned up by the NetSocket that sends it
					SendPacket*const ACK = new SendPacket(IncomingPacket->GetPacketID(), PN_KeepAlive, Address, true);
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
		inline void Send_Packet(SendPacket*const Packet) {
			Socket->PostCompletion<SendPacket*const>(CK_SEND, Packet);
		}

		inline const auto RTT_KOL() const { return Avg_RTT; }

		inline NetAddress*const GetAddress() const { return Address; }
	};
}