#include "PeerNet.h"

#pragma comment(lib, "ws2_32.lib")

namespace PeerNet
{
	// Hidden Implementation Namespace
	// Only Visible In This File
	namespace
	{
		std::forward_list<std::pair<std::string,NetSocket*>> NetSockets;

		std::mutex PeersMutex;
		std::unordered_map<std::string, const std::shared_ptr<NetPeer>> Peers;
	}

	// Public Implementation Methods
	void Initialize()
	{
		const size_t iResult = WSAStartup(MAKEWORD(2, 2), &WSADATA());
		if (iResult != 0) {
			printf("PeerNet Not Initialized Error: %i\n", iResult);
		} else {
			printf("PeerNet Initialized\n");
		}
	}

	void Deinitialize()
	{
		// Cleanup all our NetSockets beflore closing WinSock
		while (!NetSockets.empty())
		{
			// Remove the front element until none remain
			// Delete from memory then pop from list
			delete NetSockets.front().second;
			NetSockets.pop_front();
		}
		WSACleanup();
		printf("PeerNet Deinitialized\n");
	}

	NetSocket* CreateSocket(const std::string StrIP, const std::string StrPort)
	{
		// Loop through our current sockets
		// Check if were using this IP and Port yet
		//	ToDo: Broken with hostnames
		const std::string FormattedAddress(StrIP + std::string(":") + StrPort);
		bool SocketAvailable = true;
		for (auto Socket : NetSockets) {
			if (Socket.first == FormattedAddress.c_str()) {
				SocketAvailable = false;
				break;
			}
		}
		if (SocketAvailable) {
			NetSocket* NewNetSocket = new NetSocket(StrIP, StrPort);
			if (NewNetSocket) {
				//	Add it to the list
				NetSockets.push_front(std::make_pair(NewNetSocket->GetFormattedAddress(),NewNetSocket));
				#ifdef _DEBUG
				printf("NetSocket::CreateSocket - Socket Created - %s\n", FormattedAddress.c_str());
				#endif
				return NewNetSocket;
			}
		}
		#ifdef _DEBUG
		printf("NetSocket::CreateSocket - Socket Unavailable - %s\n", FormattedAddress.c_str());
		#else
		printf("Already Listening On %s\n", FormattedAddress.c_str());
		#endif
		return NULL;
	}

	void DeleteSocket(NetSocket*const Socket)
	{
		NetSockets.remove_if([Socket](const std::pair<std::string, NetSocket*> Value) {
			if (Value.first == Socket->GetFormattedAddress())
			{
				delete Socket;
				return true;
			}
			return false;
		});
	}

	//	Add a new peer
	void AddPeer(std::shared_ptr<NetPeer> Peer)
	{
		//	Grab a lock on our Peers
		PeersMutex.lock();
		//	Create a new NetPeer into the Peers variable 
		Peers.emplace(Peer->GetFormattedAddress(), Peer);
		PeersMutex.unlock();
	}

	//	Retrieve a NetPeer* from it's formatted address
	std::shared_ptr<NetPeer> GetPeer(const std::string FormattedAddress)
	{
		PeersMutex.lock();
		if (Peers.count(FormattedAddress))
		{
			auto Peer = Peers.at(FormattedAddress);
			PeersMutex.unlock();
			return Peer;
		}
		PeersMutex.unlock();
		return nullptr;
	}
}