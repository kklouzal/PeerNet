# PeerNet

Multi Threadded Winsock 2 Registered Input/Output (RIO) Peer to Peer (P2P) Networking using a Client-Server like Functional Programming model.

<img alt="AppVeyor Build Status" src="https://ci.appveyor.com/api/projects/status/ni2ttyxpcoiubt7d/branch/master?svg=true"/> <a href="https://scan.coverity.com/projects/kklouzal-peernet">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/10224/badge.svg"/>
</a>

**Each client acts a server and each server can optionally act as a client. You choose who sends data to who.**

>>Data Serialization with Cereal - https://github.com/USCiLab/cereal
>>
>>Data Compression with LZ4 - https://github.com/Cyan4973/lz4

PeerNet is entirely UDP Packet based with mechanisms in place for sending and receiving reliable packets.

Round Trip Time (RTT) for a reliable packet using localhost is 200μs (Microseconds) and 100μs for unreliable packets.
