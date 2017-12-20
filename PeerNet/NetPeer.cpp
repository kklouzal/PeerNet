#include "PeerNet.h"

namespace PeerNet
{
	NetPeer::NetPeer(NetSocket*const DefaultSocket, NetAddress*const NetAddr)
		: Address(NetAddr), Socket(DefaultSocket), RollingRTT(6), Avg_RTT(300),
		CH_KOL(new KeepAliveChannel(this, PN_KeepAlive)),
		CH_Ordered(new OrderedChannel(this, PN_Ordered)),
		CH_Reliable(new ReliableChannel(this, PN_Reliable)),
		CH_Unreliable(new UnreliableChannel(this, PN_Unreliable)),
		TimedEvent(std::chrono::milliseconds(300), 0)	//	Start with value of Avg_RTT
	{
		//	Start the Keep-Alive sequence which will initiate the connection
		this->StartTimer();
		printf("\tConnect Peer - %s\n", Address->FormattedAddress());
	}

	NetPeer::~NetPeer()
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
		printf("\tClient Tick Expire\n");
	}

	//	Send a packet
	//	External usage only and as a means to introduce a packet into a socket for transmission
	void NetPeer::Send_Packet(SendPacket*const Packet) {
		Socket->PostCompletion<SendPacket*const>(CK_SEND, Packet);
	}
}