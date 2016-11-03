#include "PeerNet.h"

namespace PeerNet
{
	//
	//	Default Constructor
	NetPeer::NetPeer(const std::string StrIP, const std::string StrPort, NetSocket*const DefaultSocket)
		: Address(new NetAddress(StrIP, StrPort)), Socket(DefaultSocket),
		CH_KOL(new ReliableChannel<PacketType::PN_KeepAlive>(this)),
		CH_Ordered(new OrderedChannel<PacketType::PN_Ordered>(this)),
		CH_Reliable(new ReliableChannel<PacketType::PN_Reliable>(this)),
		CH_Unreliable(new UnreliableChannel<PacketType::PN_Unreliable>(this)),
		TimedEvent(std::chrono::milliseconds(150), 0)	//	Clients 'Tick' every 0.15 second until they're destroyed
	{
		this->StartTimer();
		printf("Create Peer - %s\n", Address->FormattedAddress());
	}

	NetPeer::~NetPeer()
	{
		this->StopTimer();
		printf("Remove Peer - %s\n", Address->FormattedAddress());
	}

	//	BaseClass TimedEvent OnTick function
	//	Used for Keep-Alive and ACK sync
	void NetPeer::OnTick()
	{
		//	Keep-Alive Protocol:
		//
		//	(unsigned long)		Highest Received KOL Packet ID
		//	(unsigned long)		Highest Received Reliable Packet ID
		//	(unsigned long)		Highest Received && Processed Ordered Packet ID
		//	(unordered_map)*					Missing Ordered Reliable Packet ID's
		//	(std::chrono::milliseconds)*		My Reliable RTT
		//	(std::chrono::milliseconds)	*		My Reliable Ordered RTT
		//
		NetPacket* KeepAlive = CreateNewPacket(PacketType::PN_KeepAlive);
		KeepAlive->WriteData<unsigned long>(CH_KOL->GetLastID());
		KeepAlive->WriteData<unsigned long>(CH_Reliable->GetLastID());
		KeepAlive->WriteData<unsigned long>(CH_Ordered->GetLastID());
		SendPacket(KeepAlive);
	}

	//	BaseClass TimedEvent OnExpire function
	//	This should never call as a clients timed event infinitely ticks until the client is destroyed
	void NetPeer::OnExpire()
	{
		printf("Client Tick Expire\n");
	}

	//	Send a packet
	//	External usage only and as a means to introduce a packet into a socket for transmission
	void NetPeer::SendPacket(NetPacket*const Packet) { Socket->PostCompletion<NetPacket*const>(CK_SEND, Packet); }
	//
	//	Called from a NetSocket's Receive Thread
	void NetPeer::ReceivePacket(NetPacket*const IncomingPacket)
	{
		switch (IncomingPacket->GetType()) {

		case PacketType::PN_Ordered: if (CH_Ordered->Receive(IncomingPacket)) { delete IncomingPacket; } break;

		case PacketType::PN_Reliable: CH_Reliable->Receive(IncomingPacket); delete IncomingPacket; break;

		case PacketType::PN_Unreliable: CH_Unreliable->Receive(IncomingPacket); delete IncomingPacket; break;

		case PacketType::PN_KeepAlive:
			if (!CH_KOL->Receive(IncomingPacket)) { delete IncomingPacket; break; }
			//	Process this Keep-Alive Packet
			CH_KOL->ACK(IncomingPacket->ReadData<unsigned long>(), IncomingPacket->GetCreationTime());
			CH_Reliable->ACK(IncomingPacket->ReadData<unsigned long>(), IncomingPacket->GetCreationTime());
			CH_Ordered->ACK(IncomingPacket->ReadData<unsigned long>(), IncomingPacket->GetCreationTime());
			//	End Keep-Alive Packet Processing
			delete IncomingPacket;
		break;

		default:
			printf("Recv Unknown Packet Type\n");
			delete IncomingPacket;
		}
	}
}