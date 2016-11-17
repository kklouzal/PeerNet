# PeerNet

### Multi Threadded Winsock 2 Registered Input/Output (RIO) All Purpose C++ Network Library ###

<img alt="AppVeyor Build Status" src="https://ci.appveyor.com/api/projects/status/ni2ttyxpcoiubt7d/branch/master?svg=true"/> <a href="https://scan.coverity.com/projects/kklouzal-peernet">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/10224/badge.svg"/>
</a>

#### PeerNet is entirely UDP Packet based ####
 * Unreliable Packets - Only the most recently received packets of this type are processed and they have no guarentee of delivery.
 * Reliable Packets - You are guarenteed to receive every packet sent of this type, however like with Unreliable, only the most recently received packet is processed.
 * Ordered Reliable Packets - You are guarenteed to receive every packet sent and process them on the receiving side in exactly the order they were sent.

#### All packets are serialized ####
>>Data Serialization with Cereal - https://github.com/USCiLab/cereal

#### Followed by a quick compression ####
>>Data Compression with ZStd - https://github.com/facebook/zstd

#### Breakdown ####
 * NetSocket - Binds an IP/Hostname Port combination used to for send and/or receive communications.
 * NetPeer - Created locally to represent an active connection to a remote NetSocket.
  * Must be assigned at least one NetSocket for communication.
  * Assign multiple NetSocket's for Round-Robin behaviour.
 * NetChannel - Defines a channel of communication between any associated Netpeer.
  * Specify a unique path for packets of a specific type to travel.
  * Supported protocols: Unreliable, Reliable and Ordered.
 * NetPacket - Holds your data payload and may be sent to more than one NetPeer.
  * Data must be read in the same order it was written.
  * Only the data that was written must tried to be read.
  * Any type of data can be written as long as it can be serialized by Cereal.

Loopback Round Trip Time (RTT) for a reliable packet is sub-200μs (Microseconds).
LAN tests put it around 800μs - Thats less than 1ms of overhead for a packet to travel to it's destination and for the host to receive a notification of delivery!
