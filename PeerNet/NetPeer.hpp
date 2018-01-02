#pragma once
#include "TimedEvent.hpp"
#include "Channel_KeepAlive.hpp"
#include "Channel_Unreliable.hpp"
#include "Channel_Reliable.hpp"
#include "Channel_Ordered.hpp"

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

		inline virtual void Tick() = 0;
		inline virtual void Receive(ReceivePacket* Packet) = 0;

		std::deque<ReceivePacket*> ProcessingQueue_RAW;

		inline void OnTick()
		{
			//	Check to see if this peer is no longer alive
			//const unsigned long UnACK = CH_KOL->GetUnacknowledgedCount();
			//printf("UnAck %zi\n", UnACK);
			if (CH_KOL->GetUnacknowledgedCount() > 1000) {
				_PeerNet->DisconnectPeer(this);
			} else {
				//	Keep a rolling average of the last 6 values returned by CH_KOL->RTT()
				//	This spreads our RTT up to about 30 seconds for a 250ms ping
				//	And about 6 seconds for a 50ms ping
				Avg_RTT -= Avg_RTT / RollingRTT;
				Avg_RTT += CH_KOL->RTT() / RollingRTT;

				//	Send a Keep-Alive
				Send_Packet(CH_KOL->NewPacket());

				//	Delete managed packets
				CH_KOL->DeleteUsed();
				CH_Unreliable->DeleteUsed();

				//	Call Receive() on all our waiting-to-be-processed packets from each channel
				CH_Unreliable->SwapProcessingQueue(ProcessingQueue_RAW);
				while (!ProcessingQueue_RAW.empty())
				{
					ReceivePacket* Packet = ProcessingQueue_RAW.front();
					//printf("Unreliable - %d - %s\tFrom Queue\n", Packet->GetPacketID(), Packet->ReadData<std::string>().c_str());
					//	Loop through the queue and call Receive
					Receive(Packet);
					ProcessingQueue_RAW.pop_front();
					//	Cleanup the ReceivePacket
					delete Packet;

				}
				CH_Reliable->SwapProcessingQueue(ProcessingQueue_RAW);
				while (!ProcessingQueue_RAW.empty())
				{
					ReceivePacket* Packet = ProcessingQueue_RAW.front();
					//printf("Reliable - %d - %s\tFrom Queue\n", Packet->GetPacketID(), Packet->ReadData<std::string>().c_str());
					//	Loop through the queue and call Receive
					Receive(Packet);
					ProcessingQueue_RAW.pop_front();
					//	Cleanup the ReceivePacket
					delete Packet;

				}
				CH_Ordered->SwapProcessingQueue(ProcessingQueue_RAW);
				while (!ProcessingQueue_RAW.empty())
				{
					ReceivePacket* Packet = ProcessingQueue_RAW.front();
					//printf("Ordered - %d - %s\tFrom Queue\n", Packet->GetPacketID(), Packet->ReadData<std::string>().c_str());
					//	Loop through the queue and call Receive
					Receive(Packet);
					ProcessingQueue_RAW.pop_front();
					//	Cleanup the ReceivePacket
					delete Packet;

				}

				//	Call derived classes Tick() method after all packets have been processed
				Tick();

				//	Resend all unacknowledged packets
				CH_Reliable->ResendUnacknowledged(this->Socket);
				CH_Ordered->ResendUnacknowledged(this->Socket);
			}
		}
		inline void NetPeer::OnExpire()
		{
			printf("\tClient Tick Expire\n");
		}

	public:
		NetSocket*const Socket;

		bool FakePacketLoss = false;

		inline void PrintChannelStats()
		{
			CH_Reliable->PrintStats();
			CH_Ordered->PrintStats();
		}

		//	Constructor
		inline NetPeer(PeerNet* PNInstance, NetSocket*const DefaultSocket, NetAddress*const NetAddr)
			: _PeerNet(PNInstance), Address(NetAddr), Socket(DefaultSocket), RollingRTT(6), Avg_RTT(100),
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
		inline virtual ~NetPeer()
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

		//	Construct and return a reliable NetPacket to fill and send to this NetPeer
		inline SendPacket* CreateOrderedPacket(const unsigned long& OP) {
			return CH_Ordered->NewPacket(OP);
		}

		//	Construct and return a reliable NetPacket to fill and send to this NetPeer
		inline SendPacket* CreateReliablePacket(const unsigned long& OP) {
			return CH_Reliable->NewPacket(OP);
		}

		//	Construct and return a unreliable NetPacket to fill and send to this NetPeer
		inline SendPacket* CreateUnreliablePacket(const unsigned long& OP) {
			return CH_Unreliable->NewPacket(OP);
		}

		//	
		inline void Receive_Packet(const string& IncomingData)
		{
			//	Disreguard any incoming packets for this peer if our Keep-Alive sequence isnt active
			if (!TimerRunning()) { return; }

			//	Instantiate a NetPacket from our decompressed data
			ReceivePacket*const IncomingPacket = new ReceivePacket(IncomingData);

			//	Process the packet as needed
			switch (IncomingPacket->GetType()) {

			case PN_KeepAlive:
			{
				//	Process the incoming keep-alive
				if (CH_KOL->Receive(IncomingPacket)) {
					//	Send an ACK if needed
					Send_Packet(CH_KOL->NewACK(IncomingPacket, Address));
				}
				delete IncomingPacket;
				break;
			}

			case PN_Unreliable: CH_Unreliable->Receive(IncomingPacket); break;

			case PN_Reliable:
			{
				//	If a random number between 1-10 equals another random number between 1-10
				//	Drop the packet to simulate packet loss
				if (FakePacketLoss && (rand() % 10 + 1) == (rand() % 10 + 1))
				{
					delete IncomingPacket;
					break;
				}
				//	Is this an ACK?
				if (IncomingPacket->ReadData<bool>())
				{
					CH_Reliable->ACK(IncomingPacket->GetPacketID(), IncomingPacket->GetOperationID());
					delete IncomingPacket;
				}
				else {
					//	Send back an ACK
					Send_Packet(CH_Reliable->NewACK(IncomingPacket, Address));
					//	Process the packet
					CH_Reliable->Receive(IncomingPacket); 
					break;
				}
				break;
			}

				//	Ordered packeds need processed inside their Receive function
			case PN_Ordered:
			{
				//	If a random number between 1-10 equals another random number between 1-10
				//	Drop the packet to simulate packet loss
				if (FakePacketLoss && (rand() % 10 + 1) == (rand() % 10 + 1))
				{
					delete IncomingPacket;
					break;
				}
				//	Is this an ACK?
				if (IncomingPacket->ReadData<bool>())
				{
					CH_Ordered->ACK(IncomingPacket->GetPacketID(), IncomingPacket->GetOperationID());
					delete IncomingPacket;
				}
				else {
					//	Send back an ACK
					Send_Packet(CH_Ordered->NewACK(IncomingPacket, Address));
					//	Process the packet
					CH_Ordered->Receive(IncomingPacket);
				}
				break;
			}

				//	Default case for unknown packet type
			default: printf("Recv Unknown Packet Type\n"); delete IncomingPacket;
			}
		}
		inline void Send_Packet(SendPacket* Packet) {
			Socket->SendPacket(Packet);
		}

		inline const auto RTT_KOL() const { return Avg_RTT; }

		inline NetAddress*const GetAddress() const { return Address; }
	};
}