#include "PeerNet.h"

namespace PeerNet
{
	//
	//	Default Constructor
	NetPeer::NetPeer(const std::string StrIP, const std::string StrPort, NetSocket*const DefaultSocket)
		: Address(new NetAddress(StrIP, StrPort)), Socket(DefaultSocket), OrderedPkts(), OrderedAcks(),
		ReliablePkts(), IN_OrderedPkts(), IN_OrderedPktMutex(), OrderedMutex(), ReliablePktMutex(),
		TimedEvent(std::chrono::milliseconds(1000), 0)	//	Clients 'Tick' every 1 second until they're destroyed
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
		//	(unsigned long)					Highest Received Reliable Packet
		//	(unordered_map)					Missing Ordered Reliable Packet ID's
		//	(std::chrono::milliseconds)		My Reliable RTT
		//	(std::chrono::milliseconds)		My Reliable Ordered RTT
		//
		NetPacket* KeepAlive = CreateNewPacket(PacketType::PN_KeepAlive);
		KeepAlive->WriteData<unsigned long>(LatestReceivedReliable);
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
	void NetPeer::SendPacket(NetPacket*const Packet) {
		if (Packet->GetType() == PacketType::PN_Ordered)
		{
			OrderedMutex.lock();
			OrderedPkts.emplace(Packet->GetPacketID(), Packet);
			OrderedMutex.unlock();
		}
		else if (Packet->GetType() == PacketType::PN_Reliable) {
			ReliablePktMutex.lock();
			ReliablePkts.emplace(Packet->GetPacketID(), Packet);
			ReliablePktMutex.unlock();
		}
		Socket->PostCompletion<NetPacket*const>(CK_SEND, Packet);
	}
	//
	//	Called from a NetSocket's Receive Thread
	void NetPeer::ReceivePacket(NetPacket*const IncomingPacket)
	{
		switch (IncomingPacket->GetType()) {
			//	PN_OrderedACK
		case PacketType::PN_OrderedACK:
		{
			OrderedMutex.lock();
			if (IncomingPacket->GetPacketID() < NextExpectedOrderedACK) { delete IncomingPacket; break; }
			if (IncomingPacket->GetPacketID() > NextExpectedOrderedACK)
			{ OrderedAcks.emplace(IncomingPacket->GetPacketID(), IncomingPacket); OrderedMutex.unlock(); break; }
			//	This is the packet id we're looking for, increment counter and check container
			//	Sanity Check: See if a packet with this ID already exists in our in_packet container
			//		If so then delete IncomingPacket, check if packet in container gets processed
			//		If not then instead you delete the packet in the container and process this one
			auto Pkt = OrderedPkts.find(NextExpectedOrderedACK);					//	See if our ACK has a corresponding send packet
			if (Pkt == OrderedPkts.end()) { OrderedMutex.unlock(); break; }		//	Not found; break loop.
			AckOrdered(std::chrono::duration<double, std::milli>(IncomingPacket->GetCreationTime() - Pkt->second->GetCreationTime()).count());
#ifdef _DEBUG_PACKETS_ORDERED_ACK
			printf("\tOrdered Ack 1 - %i -\t %.3fms\n", IncomingPacket->GetPacketID(), GetAvgOrderedRTT());
#endif
			OrderedPkts.erase(Pkt);				//	Found; remove the send packet from the outgoing container
			++NextExpectedOrderedACK;			//	Increment our counter
			delete IncomingPacket;				//	Free the ACK's memory
			while (!OrderedAcks.empty())		//	Check any queued up ACK's
			{
				auto Ack = OrderedAcks.find(NextExpectedOrderedACK);				//	See if we've already received our next expected packet.
				if (Ack == OrderedAcks.end()) { OrderedMutex.unlock(); return; }	//	Not found; end entire function.
				auto Pkt2 = OrderedPkts.find(NextExpectedOrderedACK);				//	See if our ACK has a corresponding send packet
				if (Pkt2 == OrderedPkts.end())										//	Not found; Increment Counter; Cleanup ACK; return loop.
				{ ++NextExpectedOrderedACK; delete Ack->second; OrderedAcks.erase(Ack); OrderedMutex.unlock(); return; }
				AckOrdered(std::chrono::duration<double, std::milli>(Ack->second->GetCreationTime() - Pkt2->second->GetCreationTime()).count());
#ifdef _DEBUG_PACKETS_ORDERED_ACK
				printf("\tOrdered Ack 2 - %i - %.3fms\n", Ack->second->GetPacketID(), GetAvgOrderedRTT());
#endif
				OrderedPkts.erase(Pkt2);		//	Found; remove the send packet from the outgoing container
				++NextExpectedOrderedACK;		//	Increment counter.
				delete Ack->second;				//	Cleanup this ACK
				OrderedAcks.erase(Ack);			//	Continue until the next expected ACK is not found
			}
			OrderedMutex.unlock();
		}
		break;

			//	PN_ReliableACK
		case PacketType::PN_ReliableACK:
		{
			ReliablePktMutex.lock();
			auto got = ReliablePkts.find(IncomingPacket->GetPacketID());	//	Check if our send packet still exists for this ack
			if (got == ReliablePkts.end()) { ReliablePktMutex.unlock(); delete IncomingPacket; break; }	//	Not found; delete ack; break;
			//	ToDo: Call the send packet's callback function if one was provided; pass the ack as one of our parameters
			//	Any processing needed on ACK or Send Packet needs to be done here
			AckReliable(std::chrono::duration<double, std::milli>(IncomingPacket->GetCreationTime() - got->second->GetCreationTime()).count());
#ifdef _DEBUG_PACKETS_RELIABLE_ACK
			printf("\tReliable Ack - %i - %.3fms\n", IncomingPacket->GetPacketID(), GetAvgReliableRTT());
#endif
			ReliablePkts.erase(got);
			ReliablePktMutex.unlock();
			delete IncomingPacket;
		}
		break;

		//	PN_Ordered -- Receive an ordered packet sent from another peer
		case PacketType::PN_Ordered:
			//	Ordered packets always ACK immediatly
			SendPacket(new NetPacket(IncomingPacket->GetPacketID(), PacketType::PN_OrderedACK, this));

			//	Only accept the most recent received Ordered packets
			if (IncomingPacket->GetPacketID() < NextExpectedOrdered) { delete IncomingPacket; break; }

			//	If this packet is not next in line, store it in the container
			if (IncomingPacket->GetPacketID() > NextExpectedOrdered)
			{
#ifdef _DEBUG_PACKETS_ORDERED
				printf("Store Ordered %u - Needed %u\n", IncomingPacket->GetPacketID(), NextExpectedOrdered);
#endif
				IN_OrderedPktMutex.lock();
				IN_OrderedPkts.emplace(IncomingPacket->GetPacketID(), IncomingPacket);
				IN_OrderedPktMutex.unlock();
				break;
			}
			//	If this packet is next in line, process it, and increment our counter
			++NextExpectedOrdered;
#ifdef _DEBUG_PACKETS_ORDERED
			printf("Process Ordered - %u\n", IncomingPacket->GetPacketID());
#endif
			delete IncomingPacket;

			//	Check the container against our new counter value
			IN_OrderedPktMutex.lock();
			while (!IN_OrderedPkts.empty())
			{
				auto got = IN_OrderedPkts.find(NextExpectedOrdered);						//	See if the next expected packet is in our container
				if (got == IN_OrderedPkts.end()) { IN_OrderedPktMutex.unlock(); return; }	//	Not found; quit searching, unlock and return
				++NextExpectedOrdered;														//	Found; increment counter; process packet; continue searching
#ifdef _DEBUG_PACKETS_ORDERED
				printf("\tProcess Stored - %u\n", got->first);
#endif
				delete got->second;
				IN_OrderedPkts.erase(got);
			}
			IN_OrderedPktMutex.unlock();	//	No packets to check; unlock and break
		break;

		//	PN_Reliable
		case PacketType::PN_Reliable:
			//	Reliable packets always ACK immediatly
			//SendPacket(new NetPacket(IncomingPacket->GetPacketID(), PacketType::PN_ReliableACK, this));
			//	Only accept the most recent received reliable packets
			if (IncomingPacket->GetPacketID() <= LatestReceivedReliable) { delete IncomingPacket; break; }
			LatestReceivedReliable = IncomingPacket->GetPacketID();
#ifdef _DEBUG_PACKETS_RELIABLE
			printf("Recv Reliable - %u\n", IncomingPacket->GetPacketID());
#endif
			delete IncomingPacket;
		break;

		//	PN_Unreliable
		case PacketType::PN_Unreliable:
			//	Only accept the most recent received unreliable packets
			if (IncomingPacket->GetPacketID() <= LastReceivedUnreliable) { delete IncomingPacket; break; }
			LastReceivedUnreliable = IncomingPacket->GetPacketID();
#ifdef _DEBUG_PACKETS_UNRELIABLE
			printf("Recv Unreliable - %u\n", IncomingPacket->GetPacketID());
#endif
			delete IncomingPacket;
		break;

		//	PN_KeepAlive
		case PacketType::PN_KeepAlive:
		{
			//	Only accept the most recent received KOL packets
			if (IncomingPacket->GetPacketID() <= LastReceivedKOL) { delete IncomingPacket; break; }
			LastReceivedKOL = IncomingPacket->GetPacketID();
			//
			//	Process our Keep Alive
			const unsigned long LastReliableID = IncomingPacket->ReadData<unsigned long>();

			//	If their last received reliable ID is less than our last sent reliable id
			//	Send the most recently sent reliable packet to them again
			//	Note: if this packet is still in-transit it will be sent again
			//	ToDo:	Hold off on resending the packet until it's creation time
			//			is greater than this clients RTT
			ReliablePktMutex.lock();
			if (LastReliableID < NextReliablePacketID-1)
			{
				if (ReliablePkts.count(LastReliableID))
				{
					printf("Resend Reliable ID %i\n", LastReliableID);
					Socket->PostCompletion<NetPacket*const>(CK_SEND, ReliablePkts[LastReliableID]);
				}
			}
			//	Remove all packets in our outgoing reliable packet container
			//	With an ID less or equal to the last confirmed received ID
			for (auto Packet : ReliablePkts)
			{
				if (Packet.first <= LastReliableID)
				{
					ReliablePkts.erase(Packet.first);
				}
			}
			ReliablePktMutex.unlock();
			//
#ifdef _DEBUG_PACKETS_KEEPALIVE
			printf("Recv Keep Alive - %u\n", IncomingPacket->GetPacketID());
#endif
			delete IncomingPacket;
		}
		break;

		default:
			printf("Recv Unknown Packet Type\n");
			delete IncomingPacket;
		}
	}
}