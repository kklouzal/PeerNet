//
// pch.cpp
// Include the standard header and generate the precompiled header.
//

#include "pch.h"

#ifdef _WIN64
#pragma comment(lib, "PeerNet_x64.lib")
#else
#pragma comment(lib, "PeerNet_Win32.lib")
#endif