#include "PeerNet.h"

namespace PeerNet
{
	NetPeer::NetPeer(NetSocket*const DefaultSocket, NetAddress*const NetAddr)
		: Address(NetAddr), Socket(DefaultSocket),
		CH_KOL(new KeepAliveChannel(this, PN_KeepAlive)),
		CH_Ordered(new OrderedChannel(this, PN_Ordered)),
		CH_Reliable(new ReliableChannel(this, PN_Reliable)),
		CH_Unreliable(new UnreliableChannel(this, PN_Unreliable)),
		TimedEvent(std::chrono::milliseconds(25), 0)	//	Clients 'Tick' every 0.025 second until they're destroyed
	{
		//	Send out our discovery request
		printf("Create Discovery Packet\n");
		auto DiscoveryPacket = CreateNewPacket(PN_Reliable);
		DiscoveryPacket->WriteData<std::string>("Read - Discovery Packet\n");
		SendPacket(DiscoveryPacket.get());
		this->StartTimer();
		printf("Create Peer - %s\n", Address->FormattedAddress());
	}

	NetPeer::~NetPeer()
	{
		this->StopTimer();
		delete CH_KOL;
		delete CH_Ordered;
		delete CH_Reliable;
		delete CH_Unreliable;
		printf("Remove Peer - %s\n", Address->FormattedAddress());
	}

	//	BaseClass TimedEvent OnTick function
	//	Used for Keep-Alive and ACK sync
	void NetPeer::OnTick()
	{
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
		SendPacket(KeepAlive.get());
	}

	//	BaseClass TimedEvent OnExpire function
	//	This should never call as a clients timed event infinitely ticks until the client is destroyed
	void NetPeer::OnExpire()
	{
		printf("Client Tick Expire\n");
	}

	//	Send a packet
	//	External usage only and as a means to introduce a packet into a socket for transmission
	void NetPeer::SendPacket(NetPacket* Packet) {
		Socket->PostCompletion<NetPacket*>(CK_SEND, Packet);
	}
	//
	//	Called from a NetSocket's Receive Thread
	void NetPeer::ReceivePacket(NetPacket* IncomingPacket)
	{
		switch (IncomingPacket->GetType()) {

		case PN_KeepAlive:
			if (CH_KOL->Receive(IncomingPacket))
			{
				//	Process this Keep-Alive Packet
				//	Memory for the ACK is cleaned up by the NetSocket that sends it
				NetPacket* ACK = new NetPacket(IncomingPacket->GetPacketID(), PN_KeepAlive, this, true);
				ACK->WriteData<bool>(false);
				SendPacket(ACK);

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
				printf("Unreliable - %s\n", IncomingPacket->ReadData<std::string>().c_str());
				//	For now just delete the IncomingPacket
				delete IncomingPacket;
			}
		break;
		case PN_Reliable:
			if (CH_Reliable->Receive(IncomingPacket))
			{
				//	Call packet's callback function?
				printf("Reliable - %s", IncomingPacket->ReadData<std::string>().c_str());
				//	For now just delete the IncomingPacket
				delete IncomingPacket;
			}
		break;
		case PN_Ordered:
			if (CH_Ordered->Receive(IncomingPacket))
			{
				//	Call packet's callback function?
				printf("Ordered - %s", IncomingPacket->ReadData<std::string>().c_str());
				//	For now just delete the IncomingPacket
				delete IncomingPacket;
			}
		break;
		default:	printf("Recv Unknown Packet Type\n");
		}
	}
}