#include "PeerNet.h"
#pragma comment(lib, "ws2_32.lib")

namespace PeerNet
{
	// Public Implementation Methods
	void Initialize()
	{
		const size_t iResult = WSAStartup(MAKEWORD(2, 2), &WSADATA());
		if (iResult != 0) {
			printf("PeerNet Not Initialized Error: %i\n", (int)iResult);
		} else {
			printf("PeerNet Initialized\n");
		}
	}

	void Deinitialize()
	{
		WSACleanup();
		printf("PeerNet Deinitialized\n");
	}
}