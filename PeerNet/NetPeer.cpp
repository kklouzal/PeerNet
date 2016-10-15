#include "PeerNet.h"

namespace PeerNet
{
	//
	//	Default Constructor
	NetPeer::NetPeer(const std::string StrIP, const std::string StrPort, NetSocket*const DefaultSocket)
		: Address(new NetAddress(StrIP, StrPort)), Socket(DefaultSocket),OrderedPkts(), OrderedAcks(),
		ReliablePkts(), IN_OrderedPkts(), OrderedPktMutex(), IN_OrderedPktMutex(), OrderedAckMutex(), ReliablePktMutex()
	{
		//	Send out our discovery request
		SendPacket(CreateNewPacket(PacketType::PN_Reliable));
		printf("Create Peer - %s\n", Address->FormattedAddress());
	}

	//	Construct and return a NetPacket to fill and send to this NetPeer
	//	ToDo: PacketID will clash on socket if same socket is used to send packets for two different peers and each peer chooses the same PacketID
	NetPacket*const NetPeer::CreateNewPacket(const PacketType pType) {
		if (pType == PacketType::PN_Ordered)
		{
			return new NetPacket(NextOrderedPacketID++, pType, this);;
		}
		else if (pType == PacketType::PN_Reliable)
		{
			return new NetPacket(NextReliablePacketID++, pType, this);;
		}
		return new NetPacket(NextUnreliablePacketID++, pType, this);
	}

	//	Send a packet
	//	External usage only and as a means to introduce a packet into a socket for transmission
	void NetPeer::SendPacket(NetPacket*const Packet) {
		if (Packet->GetType() == PacketType::PN_Ordered)
		{
			OrderedPktMutex.lock();
			OrderedPkts.emplace(Packet->GetPacketID(), Packet);
			OrderedPktMutex.unlock();
		}
		else if (Packet->GetType() == PacketType::PN_Reliable) {
			ReliablePktMutex.lock();
			ReliablePkts.emplace(Packet->GetPacketID(), Packet);
			ReliablePktMutex.unlock();
		}
		Socket->PostCompletion<NetPacket*>(CK_SEND, Packet);
	}
	//
	//	Called from a NetSocket's Receive Thread
	void NetPeer::ReceivePacket(NetPacket*const IncomingPacket)
	{
		switch (IncomingPacket->GetType()) {
			//	PN_OrderedACK
		case PacketType::PN_OrderedACK:
		{
			printf("RCV ORDER ACK %u\n", IncomingPacket->GetPacketID());
			if (IncomingPacket->GetPacketID() < NextExpectedOrderedACK) { delete IncomingPacket; break; }
			if (IncomingPacket->GetPacketID() > NextExpectedOrderedACK)
			{
				printf("Store Ordered ACK %i - Need %i\n", IncomingPacket->GetPacketID(), NextExpectedOrderedACK);
				OrderedAckMutex.lock(); OrderedAcks.emplace(IncomingPacket->GetPacketID(), IncomingPacket); OrderedAckMutex.unlock(); break;
			}
			//	This is the packet id we're looking for, increment counter and check container
			//	Sanity Check: See if a packet with this ID already exists in our in_packet container
			//		If so then delete IncomingPacket, check if packet in container gets processed
			//		If not then instead you delete the packet in the container and process this one
			OrderedPktMutex.lock();
			auto Pkt = OrderedPkts.find(NextExpectedOrderedACK);					//	See if our ACK has a corresponding send packet
			if (Pkt == OrderedPkts.end()) { OrderedPktMutex.unlock(); break; }		//	Not found; break loop.
#ifdef _DEBUG_PACKETS_ORDERED_ACK
			printf("\tOrdered Ack 1 - %i -\t %.3fms\n", IncomingPacket->GetPacketID(),
				(std::chrono::duration<double, std::milli>(Pkt->second->GetCreationTime() - IncomingPacket->GetCreationTime()).count()));
#endif
			OrderedPkts.erase(Pkt);				//	Found; remove the send packet from the outgoing container
			OrderedPktMutex.unlock();			//
			++NextExpectedOrderedACK;			//	Increment our counter
			delete IncomingPacket;				//	Free the ACK's memory
			OrderedAckMutex.lock();				//
			while (!OrderedAcks.empty())		//	Check any queued up ACK's
			{
				printf("Ordered ACKs\n");
				auto Ack = OrderedAcks.find(NextExpectedOrderedACK);				//	See if we've already received our next expected packet.
				if (Ack == OrderedAcks.end()) { OrderedAckMutex.unlock(); return; }	//	Not found; return loop.
				OrderedPktMutex.lock();												//
				auto Pkt2 = OrderedPkts.find(NextExpectedOrderedACK);				//	See if our ACK has a corresponding send packet
				if (Pkt2 == OrderedPkts.end())										//	Not found; Increment Counter; Cleanup ACK; return loop.
				{ ++NextExpectedOrderedACK; delete Ack->second; OrderedAcks.erase(Ack); OrderedPktMutex.unlock(); return; }
#ifdef _DEBUG_PACKETS_ORDERED_ACK
				printf("\tOrdered Ack 2 - %i -\t %.3fms\n", Ack->second->GetPacketID(),
					(std::chrono::duration<double, std::milli>(Pkt2->second->GetCreationTime() - Ack->second->GetCreationTime()).count()));
#endif
				OrderedPkts.erase(Pkt2);		//	Found; remove the send packet from the outgoing container
				OrderedPktMutex.unlock();		//
				++NextExpectedOrderedACK;		//	Increment counter.
				delete Ack->second;				//	Cleanup this ACK
				OrderedAcks.erase(Ack);			//	Continue until the next expected ACK is not found
			}
			OrderedAckMutex.unlock();
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
#ifdef _DEBUG_PACKETS_RELIABLE_ACK
			printf("\tReliable Ack - %i -\t %.3fms\n", IncomingPacket->GetPacketID(),
				(std::chrono::duration<double, std::milli>(got->second->GetCreationTime() - IncomingPacket->GetCreationTime()).count()));
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
			SendPacket(new NetPacket(IncomingPacket->GetPacketID(), PacketType::PN_ReliableACK, this));
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

		default:
			printf("Recv Unknown Packet Type\n");
			delete IncomingPacket;
		}
	}
}