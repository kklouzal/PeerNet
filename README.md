# PeerNet

Multi Threadded Winsock 2 Registered Input/Output (RIO) Peer to Peer (P2P) OR Client to Server.

<img alt="AppVeyor Build Status" src="https://ci.appveyor.com/api/projects/status/ni2ttyxpcoiubt7d/branch/master?svg=true"/> <a href="https://scan.coverity.com/projects/kklouzal-peernet">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/10224/badge.svg"/>
</a>

**Each client acts a server and each server can optionally act as a client. You choose who sends data to who.**


PeerNet is entirely UDP Packet based.
Unreliable - Only the most recently received packets of this type are processed and they have no guarentee of delivery.
Reliable Packets - You are guarenteed to receive every packet sent of this type, however like with Unreliable, only the most recently received packet is processed.
Ordered Reliable Packets - You are guarenteed to receive every packet sent and process them on the receiving side in exactly the order they were sent. The best of both worlds.

All packets are serialized.
>>Data Serialization with Cereal - https://github.com/USCiLab/cereal

Followed by a quick compression.
>>Data Compression with LZ4 - https://github.com/Cyan4973/lz4

Round Trip Time (RTT) for a reliable packet using localhost is 200μs (Microseconds) and 100μs for unreliable packets.
