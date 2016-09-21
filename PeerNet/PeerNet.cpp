#include "PeerNet.h"

#pragma comment(lib, "ws2_32.lib")

namespace PeerNet
{
	// Hidden Implementation Namespace
	// Only Visible In This File
	namespace
	{
		unsigned long NextPacketID = 1;
		std::forward_list<std::pair<std::string,NetSocket*>> NetSockets;
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

	NetPacket* CreateNewPacket(PacketType pType)
	{
		return new NetPacket(NextPacketID++, pType);
	}

	NetSocket* CreateSocket(const std::string StrIP, const std::string StrPort)
	{
		// Loop through our current sockets
		// Check if were using this IP and Port yet
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
				NetSockets.push_front(std::make_pair(FormattedAddress,NewNetSocket));
				return NewNetSocket;
			}
		}
		printf("PeerNet Socket %s Already Exists\n", FormattedAddress.c_str());
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
}