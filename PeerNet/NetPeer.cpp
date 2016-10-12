#include "PeerNet.h"

namespace PeerNet
{
	//
	//	Default Constructor
	NetPeer::NetPeer(const std::string StrIP, const std::string StrPort, NetSocket*const DefaultSocket)
		: Address(new NetAddress(StrIP, StrPort)), Socket(DefaultSocket), OrderedPkts(), OrderedAcks(), ReliablePkts(), ReliableAcks()
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
	void NetPeer::SendPacket(NetPacket* Packet) {
		if (Packet->GetType() == PacketType::PN_Ordered)
		{
			//	First time being sent; add packet to container; send; increment counter; return;
			if (Packet->SendAttempts == 0) {
				OrderedPkts.insert(std::make_pair(Packet->GetPacketID(), Packet));
				Socket->PostCompletion<NetPacket*>(CK_SEND, Packet);
				++Packet->SendAttempts; return;
			}

			auto got = OrderedAcks.find(Packet->GetPacketID());
			//	Matching ACK exists
			if (got != OrderedAcks.end()) {
				//	We've received an ACK however there are still packets with lower id's left to process; return;
				if (Packet->GetPacketID() >= NextExpectedOrderedACK) { return; }
				//	This packet and ACK can be deleted
				printf("\tOrdered - %i -\t %.3fms\n", Packet->GetPacketID(), std::chrono::duration<double, std::milli>(got->second->GetCreationTime() - Packet->GetCreationTime()).count());
				//	Cleanup the ACK
				delete got->second;
				OrderedAcks.erase(got);
				//	Erase packet from the outgoing containers
				OrderedPkts.erase(Packet->GetPacketID());
			}
			Socket->PostCompletion<NetPacket*>(CK_SEND, Packet);
			return;
		}
		else if (Packet->GetType() == PacketType::PN_Reliable) {
			ReliablePkts.insert(std::make_pair(Packet->GetPacketID(), Packet));
			Socket->PostCompletion<NetPacket*>(CK_SEND, Packet);
			return;
		}
		Socket->PostCompletion<NetPacket*>(CK_SEND, Packet);
	}
	//
	//	Called from a NetSocket's Receive Thread
	void NetPeer::ReceivePacket(NetPacket* IncomingPacket)
	{
		switch (IncomingPacket->GetType()) {

			//
			//
			//
			//	PN_OrderedACK
			//	Acknowledgements are passed to the NetPeer for further handling
		case PacketType::PN_OrderedACK:
			if (IncomingPacket->GetPacketID() < NextExpectedOrderedACK) { delete IncomingPacket; return; }

			OrderedAcks.insert(std::make_pair(IncomingPacket->GetPacketID(), IncomingPacket));
			//	is packet id > expected id? return.
			if (IncomingPacket->GetPacketID() > NextExpectedOrderedACK) { return; }

			//	is packet id == expected id? process packet. increment expected id.
			++NextExpectedOrderedACK;
			//
			//	Process your reliable packet here
			//
#ifdef _DEBUG_PACKETS_ORDERED_ACK
			printf("Recv Ordered Ack 1 - %u\n", IncomingPacket->GetPacketID());
#endif
			while (!OrderedAcks.empty())
			{
				auto got = OrderedAcks.find(NextExpectedOrderedACK);	//	See if we've already received our next expected packet.
				if (got == OrderedAcks.end()) { return; }	//	Not found; break loop.
				++NextExpectedOrderedACK;	//	Found; Increment our counter.
											//
											//	Process your reliable packet here
											//
											//	got->second
											//
#ifdef _DEBUG_PACKETS_ORDERED_ACK
				printf("Recv Ordered Ack 2 - %u\n", got->second->GetPacketID());
#endif
				//	Continue the loop until we run out of matches or our queue winds up empty.
			}
			break;

			//
			//
			//
			//	PN_ReliableACK
			//	Acknowledgements are passed to the NetPeer for further handling
		case PacketType::PN_ReliableACK:
			if (IncomingPacket->GetPacketID() <= LatestReceivedReliableACK) { delete IncomingPacket; break; }
			ReliableAcks.insert(std::make_pair(IncomingPacket->GetPacketID(), IncomingPacket));
			LatestReceivedReliableACK = IncomingPacket->GetPacketID();
#ifdef _DEBUG_PACKETS_RELIABLE_ACK
			printf("Recv Reliable Ack - %u\n", IncomingPacket->GetPacketID());
#endif
			break;

			//
			//
			//
			//	PN_Ordered
			//	Ordered packets wait and pass received packets numerically to peers
		case PacketType::PN_Ordered:
		{
			SendPacket(new NetPacket(IncomingPacket->GetPacketID(), PacketType::PN_OrderedACK, this));

			//	is packet id less than expected id? delete. it's already acked
			if (IncomingPacket->GetPacketID() < NextExpectedOrderedID) { delete IncomingPacket; return; }

			//	is packet id > expected id? store in map, key is packet id.
			if (IncomingPacket->GetPacketID() > NextExpectedOrderedID) { OrderedPkts.insert(std::make_pair(IncomingPacket->GetPacketID(), IncomingPacket)); return; }

			//	is packet id == expected id? process packet. delete. increment expected id.
			++NextExpectedOrderedID;
			//
			//	Process your reliable packet here
			//
#ifdef _DEBUG_PACKETS_ORDERED
			printf("Recv Ordered Packet - %u\n", IncomingPacket->GetPacketID());
#endif
			delete IncomingPacket;
			while (!OrderedPkts.empty())
			{
				auto got = OrderedPkts.find(NextExpectedOrderedID);	//	See if we've already received our next expected packet.
				if (got == OrderedPkts.end()) { return; }	//	Not found; break loop.
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
				OrderedPkts.erase(got);
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
			//	Unreliable packets are given to peers reguardless the condition
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
	
	//
	//	Final step before a reliable packet is sent or resent
	const bool NetPeer::SendPacket_Reliable(NetPacket* Packet)
	{
		//	No ack received yet; check packet if needs delete.
		if (Packet->GetPacketID() > LatestReceivedReliableACK) { return !Packet->NeedsDelete(); }

		//	Check if we have an ack packet tucked away
		auto got = ReliableAcks.find(Packet->GetPacketID());	//	See if we've already received our next expected packet.
		if (got == ReliableAcks.end()) { return false; }	//	Not found; break loop.
															//Performance Counting here
		printf("\tReliable - %i -\t %.3fms\n", Packet->GetPacketID(), (std::chrono::duration<double, std::milli>(got->second->GetCreationTime() - Packet->GetCreationTime()).count()));
		//	We're finished with the ack packet; clean it up.
		delete got->second;
		ReliableAcks.erase(got);
		return false; // we will now be deleted
	}
}