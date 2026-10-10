# Project P2 - Reliable Data Transfer

- Name: Jacob Smith
- Email: jacobsmith214@u.boisestate.edu
- Class: CS 425 001  

## Design

My program is divided into three layers to keep the different parts of the reliable data transfer process organized.

**Packet Layer:** Handles creating and decoding packets, calculating checksums, and checking for corrupted or invalid data. This keeps packet formatting separate from the transfer logic.

**Protocol Layer:** Manages the Go-Back-N sender and receiver states, including sequence numbers, acknowledgments, window sizes, and timeouts. This layer handles reliability without needing to know how packets are sent over the network.

**I/O Layer:** Handles UDP communication, connecting to the relay, reading and writing files, and running the sender and receiver. It uses the other two layers to complete the file transfer.

Separating these layers helps to make the program easier to test, debug, and maintain because each layer has its own responsibilities, as it is easier to understand in parts rather then all together. 

## Results

| Window | Loss | Run 1 (s) | Run 2 (s) | Run 3 (s) | Mean (s) | Throughput (KiB/s) |
|---|---|---|---|---|---|---|
| 1 | 0% | 103.100 | 103.203 | 103.383 | 103.229 | 9.92 |
| 16 | 0% | 6.678 | 6.696 | 6.726 | 6.700 | 152.84 |
| 1 | 5% | 131.089 | 136.929 | 126.077 | 131.365 | 7.80 |
| 16 | 5% | 27.415 | 22.552 | 27.373 | 25.780 | 39.72 |

*(Throughput = 1024 KiB / Mean Time)* 

**Round-Trip Time:** With 1,025 packets and an average time of 103.229 seconds for Window 1 without loss, the estimated RTT was 100.7 ms. This is slightly above the relay's expected 100 ms due to processing overhead.

**Difference in Window Size:** Increasing the window from 1 to 16 improved performance by about 15.4 times without loss, since multiple packets could be sent before waiting for acknowledgments. The improvement wasn't exactly 16 times because of additional processing overhead.

**Packet Loss:** At 5% loss, both configurations took longer, but Window 16 experienced a larger proportional slowdown. This is because Go-Back-N retransmits unacknowledged packets after a timeout, creating additional network traffic. Even with these retransmissions, Window 16 remained faster overall.

## Known Bugs or Issues

There are no known bugs or issues in my code.

## Experience

Overall I enjoyed this project. My C skills are getting better with
the more challenging coding projects. It was cool creating my own data  
transfer software. I enjoyed having to have 3 terminals opened one for
the python script, one for my reciever, and another for my sender. It
was cool implementing UDP and Go-Back-N as they really work hand and hand.
One thing I struggled with like all these project so far is just coverage,
especially working across branches, trying to make sure everything is  
covered is 50/50 and a bunch of trial and error, but it feels great to
see it complete. This project really helped me learn more about packets,
and data transfers securely. Overall a great project and it was fun to make.
