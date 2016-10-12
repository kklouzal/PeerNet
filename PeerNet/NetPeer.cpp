#include "PeerNet.h"

namespace PeerNet
{
	//
	//	Default Constructor
	NetPeer::NetPeer(const std::string StrIP, const std::string StrPort, NetSocket*const DefaultSocket)
		: Address(new NetAddress(StrIP, StrPort)), Socket(DefaultSocket), OrderedPkts(), OrderedAcks(), ReliablePkts()
	{
		//	Send out our discovery request
		SendPacket(CreateNewPacket(PacketType::PN_Reliable));
		printf("Create Peer - %s\n", Address->FormattedAddress());
	}

	//	Construct and return a NetPacket to fill and send to this NetPeer
	//	ToDo: PacketID will clash on socket if same socket is used to send packets for two different peers and each peer chooses the same PacketID
	NetPacket* NetPeer::CreateNewPacket(PacketType pType) {
		if (pType == PacketType::PN_Ordered)
		{
			NetPacket* NewPacket = new NetPacket(NextOrderedPacketID++, pType, this);
			OrderedPkts.insert(std::make_pair(NewPacket->GetPacketID(), NewPacket));
			return NewPacket;
		}
		else if (pType == PacketType::PN_Reliable)
		{
			NetPacket* NewPacket = new NetPacket(NextReliablePacketID++, pType, this);
			ReliablePkts.insert(std::make_pair(NewPacket->GetPacketID(), NewPacket));
			return NewPacket;
		}
		return new NetPacket(NextUnreliablePacketID++, pType, this);
	}

	//	Send a packet
	//	External usage only and as a means to introduce a packet into a socket for transmission
	void NetPeer::SendPacket(NetPacket* Packet) {
		if (Packet->GetType() == PacketType::PN_Ordered)
		{
			OrderedPkts.insert(std::make_pair(Packet->GetPacketID(), Packet));
		}
		else if (Packet->GetType() == PacketType::PN_Reliable) {
			ReliablePkts.insert(std::make_pair(Packet->GetPacketID(), Packet));
		}
		Socket->PostCompletion<NetPacket*>(CK_SEND, Packet);
	}
	//
	//	Called from a NetSocket's Receive Thread
	void NetPeer::ReceivePacket(NetPacket* IncomingPacket)
	{
		switch (IncomingPacket->GetType()) {
			//	PN_OrderedACK
		case PacketType::PN_OrderedACK:
		{
			auto FoundACK1 = OrderedPkts.find(IncomingPacket->GetPacketID());	//	Check if our send packet still exists for this ack
			if (FoundACK1 == OrderedPkts.end()) { delete IncomingPacket; break; }	//	Not found; delete ack; break;
			if (IncomingPacket->GetPacketID() > NextExpectedOrderedACK) { OrderedAcks.insert(std::make_pair(IncomingPacket->GetPacketID(), IncomingPacket)); } //	Store the ACK

			if (IncomingPacket->GetPacketID() == NextExpectedOrderedACK)
			{
				++NextExpectedOrderedACK;	//	Increment the counter so other threads can process newer packets quicker
				//	ToDo: Call the send packet's callback function if one was provided; pass the ack as one of our parameters
				//	Any processing needed on ACK or Send Packet needs to be done here
#ifdef _DEBUG_PACKETS_ORDERED_ACK
				printf("\tOrdered Ack 1 - %i -\t %.3fms\n", IncomingPacket->GetPacketID(),
					(std::chrono::duration<double, std::milli>(FoundACK1->second->GetCreationTime() - IncomingPacket->GetCreationTime()).count()));
#endif
				//	End Processing
				OrderedPkts.erase(FoundACK1);
				delete IncomingPacket;
				while (!OrderedAcks.empty())	//	Since we incremented our counter, loop through the container and find any packets that match the counters new value
				{
					auto FoundACK2 = OrderedAcks.find(NextExpectedOrderedACK);	//	See if we've already received our next expected ACK stored in the container
					if (FoundACK2 == OrderedAcks.end()) { return; }	//	Not found; break loop.
					auto FoundPacket = OrderedPkts.find(NextExpectedOrderedACK);	//	See if the corresponding Ordered Packet is stored in it's container
					if (FoundPacket == OrderedPkts.end()) { delete FoundACK2->second; OrderedAcks.erase(FoundACK2); return; }	//	Not found; Remove ACK; break loop.
					++NextExpectedOrderedACK;	//	Found both; Increment our counter.
					//	ToDo: Call the send packet's callback function if one was provided; pass the ack as one of our parameters
					//	Any processing needed on ACK or Send Packet needs to be done here
												//	FoundPacket = SendPacket; FoundACK2 = ACK
#ifdef _DEBUG_PACKETS_ORDERED_ACK
					printf("\tOrdered Ack 2 - %i -\t %.3fms\n", IncomingPacket->GetPacketID(),
						(std::chrono::duration<double, std::milli>(FoundACK2->second->GetCreationTime() - FoundPacket->second->GetCreationTime()).count()));
#endif
					//	End Processing
					//	Cleanup ACK and release the Ordered Packet
					delete FoundACK2->second;
					OrderedAcks.erase(FoundACK2);
					OrderedPkts.erase(FoundPacket);
				}
			}
		}
		break;

			//	PN_ReliableACK
		case PacketType::PN_ReliableACK:
		{
			auto got = ReliablePkts.find(IncomingPacket->GetPacketID());	//	Check if our send packet still exists for this ack
			if (got == ReliablePkts.end()) { delete IncomingPacket; break; }	//	Not found; delete ack; break;
			//	ToDo: Call the send packet's callback function if one was provided; pass the ack as one of our parameters
			//	Any processing needed on ACK or Send Packet needs to be done here
#ifdef _DEBUG_PACKETS_RELIABLE_ACK
			printf("\tReliable Ack - %i -\t %.3fms\n", IncomingPacket->GetPacketID(),
				(std::chrono::duration<double, std::milli>(got->second->GetCreationTime() - IncomingPacket->GetCreationTime()).count()));
#endif
			ReliablePkts.erase(got);
			delete IncomingPacket;
		}
		break;

		//	PN_Ordered
		case PacketType::PN_Ordered:
		{
			SendPacket(new NetPacket(IncomingPacket->GetPacketID(), PacketType::PN_OrderedACK, this));
			//	is packet id less than expected id? delete. it's already acked
			if (IncomingPacket->GetPacketID() < NextExpectedOrderedID) { delete IncomingPacket; return; }
			//	is packet id > expected id? store in map, key is packet id.
			if (IncomingPacket->GetPacketID() > NextExpectedOrderedID) { OrderedPkts_Receive.insert(std::make_pair(IncomingPacket->GetPacketID(), IncomingPacket)); return; }
			//	is packet id == expected id? process packet. delete. increment expected id.
			++NextExpectedOrderedID;
			//	Process your received ordered packet here
			//
#ifdef _DEBUG_PACKETS_ORDERED
			printf("Recv Ordered Packet - %u\n", IncomingPacket->GetPacketID());
#endif
			//	End Processing
			delete IncomingPacket;
			while (!OrderedPkts_Receive.empty())
			{
				auto got = OrderedPkts_Receive.find(NextExpectedOrderedID);	//	See if we've already received our next expected packet.
				if (got == OrderedPkts_Receive.end()) { return; }	//	Not found; break loop.
				++NextExpectedOrderedID;	//	Found; Increment our counter.
											//
											//	Process your reliable packet here
											//
											//	got->second
											//
#ifdef _DEBUG_PACKETS_ORDERED
				printf("Recv Ordered Packet - %u\n", got->second->GetPacketID());
#endif
				//	We're finished with this packet; clean it up.
				delete got->second;
				OrderedPkts_Receive.erase(got);
				//	Continue the loop until we run out of matches or our queue winds up empty.
			}
		}
		break;

		//
		//
		//
		//	PN_Reliable
		//	Reliable packets always ACK immediatly
		case PacketType::PN_Reliable:
			SendPacket(new NetPacket(IncomingPacket->GetPacketID(), PacketType::PN_ReliableACK, this));
			//	Only accept the most recent received reliable packets
			if (IncomingPacket->GetPacketID() <= LatestReceivedReliable) { delete IncomingPacket; break; }
			LatestReceivedReliable = IncomingPacket->GetPacketID();
#ifdef _DEBUG_PACKETS_RELIABLE
			printf("Recv Reliable - %u\n", IncomingPacket->GetPacketID());
#endif
			delete IncomingPacket;
			break;

			//
			//
			//
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