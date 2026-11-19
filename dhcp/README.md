### What we are building?
- DHCP server
    - Listens UDP, IPv4 on port 67
- DHCP client
    - Sends UDP, IPv4 from port 68
    - [ Ethernet Header (14B) (Link layer ) ] -> [ IPv4 Header (20B) (Network Layer) ] -> [ UDP Header (8B) (Transport Layer)] -> [ DHCP Payload (240B+) (Application Layer) ]


### How to test the client-server model on linux??
- We can not test like normal application because the DHCP client will be running on linux host and sending the DHCP request to router.
- To force it to send its request to our custom DHCP server, we have to use another custom networking namespace.
- And use the namespace to operate the client and server.
- Create an isolated namespace for the client:
```bash
sudo ip netns add dhcp_client_ns
```
- Create a virtual cable (veth0 ↔ veth1) connecting your host to the namespace:
```bash
sudo ip link add veth_server type veth peer name veth_client
sudo ip link set veth_client netns dhcp_client_ns
```
- Configure your server's interface on the main host:
```bash
sudo ip addr add 192.168.50.1/24 dev veth_server
sudo ip link set veth_server up
```
- Bring up the client interface inside the namespace (with no IP):
```bash
sudo ip netns exec dhcp_client_ns ip link set lo up
sudo ip netns exec dhcp_client_ns ip link set veth_client up
```

- Launch Your Host-Compiled Client Inside dhcp_client_ns
```bash
sudo ip netns exec dhcp_client_ns ./your_dhcp_client <ifname>
```

- All these operations will happen inside custom networking namespace `dhcp_client_ns`
- Bind the client socket to use `veth_client` and server socket to use `veth_server` and all the communication will take place between client and server. As `veth_server` and `veth_client` are wired as two ends of a cable.

### Seeing the correct data in veth_server after disabling rp filter and enabling iptable input port 67. I am confused, what is rp filter?? ANd why we needed to enable ip table port.
- Both rp_filter and iptables are built-in Linux security defenses designed to protect normal servers from malicious traffic—and both happen to treat a raw DHCPDISCOVER packet (0.0.0.0 → 255.255.255.255) as suspicious by default.
- What is `rp_filter` (Reverse path filtering)?
    - `rp_filter` is an anti-spoofing security feature inside the Linux kernel. Its job is to verify that every incoming packet has a legitimate, routable Source IP address.
    - **How it normally works**: When a packet arrives on veth_server, the kernel looks at its Source IP and asks its own routing table: "If I wanted to send a reply back to this Source IP, would I send it out through veth_server?" If the answer is "no" (or "I don't have a valid route for that IP"), the kernel assumes a hacker is forging fake Source IPs and silently drops the packet before any C program sees it.
    - **Why it blocked your DHCP packet**: Your client's DHCPDISCOVER packet has a Source IP of 0.0.0.0 (because it has no IP yet). Because veth_server is configured for 192.168.50.0/24 and 0.0.0.0 is an unroutable placeholder address, rp_filter flagged your packet as a spoofed source and threw it away.
    - **What rp_filter=0 did**: Setting it to 0 told the kernel: *"Trust all packets arriving on veth_server even if their Source IP (0.0.0.0) isn't in my routing table yet."*
    ```bash
    sudo sysctl -w net.ipv4.conf.all.rp_filter=0
    sudo sysctl -w net.ipv4.conf.veth_server.rp_filter=0
    ```

- Why We Needed the iptables Port 67 Rule?
    - `iptables` (or `nftables` / `ufw`) is the Linux host firewall. Even after `rp_filter` lets a packet pass the routing check, the firewall inspects every packet before handing it to a user-space socket (`recvfrom()`).
    - **Why it was blocked on your machine**: Remember from your ss output earlier that you have virbr0 (dnsmasq) running on your system (used by KVM/QEMU/libvirt virtual machines). When libvirt sets up virbr0, it often configures strict default firewall rules on the host's INPUT chain so random untrusted interfaces cannot send DHCP requests (UDP port 67) to your host machine.
    - **Why tcpdump saw it anyway**: tcpdump taps into the network card driver at Layer 2 before the packet reaches rp_filter or iptables. That is why tcpdump showed your packet arriving safely while your C program never received it.
    - **What the iptables command did**: Adding -I INPUT 1 -i veth_server -p udp --dport 67 -j ACCEPT placed a rule at the very top (#1) of the firewall's incoming checklist saying: *"Allow UDP traffic on port 67 arriving from veth_server to reach my C program."*
    ```bash
    sudo iptables -I INPUT 1 -i veth_server -p udp --dport 67 -j ACCEPT
    ```

Summary of the Packet's Journey Through Linux

1. **Network Card (veth_server)**: Receives raw frame → tcpdump sees it here.
2. **Routing Check (rp_filter)**: Checks if Src IP 0.0.0.0 is valid → Previously dropped here.
3. **Host Firewall (iptables INPUT)**: Checks if UDP port 67 is allowed on veth_server → Previously dropped here.
4. **Your C Socket (recvfrom)**: Delivers the DHCP payload to your while(1) loop → Working now.


### DHCP offer steps performed by DHCP server.
- The 5-Step IP Selection Priority (RFC 2131)
    - When your C DHCP server receives a DHCPDISCOVER from a client MAC (chaddr), it checks these conditions in exact order:
    - **Static Reservations (MAC-to-IP Pinning)**: First, check if the client's MAC address (chaddr) has a hardcoded static reservation in your configuration (for example, AA:BB:CC:11:22:33 always gets 192.168.50.10). If found, offer that IP immediately.
    - **Existing Active or Expired Lease for This MAC**: Search your lease table for chaddr. If this device already holds an active lease—or had an expired lease whose IP hasn't been given to anyone else yet—offer the exact same IP so the device keeps a stable address.
    - **Client's Requested IP (Option 50)**: Check if the client included Option 50 (Requested IP Address) inside its DHCPDISCOVER (clients do this when rebooting to ask for their old IP back). If that IP is inside your pool and currently FREE, offer it.
    - **First Available FREE IP in the Pool**: Scan your configured IP range (e.g., 192.168.50.100 to 192.168.50.200) for the first address marked FREE.
    - **Reclaim the Oldest Expired Lease**: If every IP in the pool has been used at least once, reclaim the IP whose expires_at timestamp is in the past (current_time > expires_at) and assign it to the new client. *(If all IPs are actively bound and unexpired, the pool is exhausted and the server drops the request.)*

- Conflict Detection Before Offering
    - Before putting the chosen IP into `yiaddr` of the DHCPOFFER, production DHCP servers perform a quick conflict check:
        - Ping / ARP Probe: The server sends an ARP request or ICMP Echo (Ping) to the candidate IP.
        - Why: If someone manually configured a static IP (like `192.168.50.100`) on a device without telling the DHCP server, the probe gets a reply. Your server marks that IP as `CONFLICT / ABANDONED` and moves on to `192.168.50.101`.

### What should be the IP header destination addr in case of DHCP offer, I guess it should be the broad cast reply, right?
- Yes, for your setup, the IP header destination address for a DHCPOFFER should be 255.255.255.255 (Limited Broadcast) on UDP port 68.
- **The Client Has No IP Yet**: At the DHCPOFFER stage, the client's network interface is still unconfigured (0.0.0.0). If your server sends a standard UDP packet addressed to the newly offered IP (192.168.50.100), the Linux kernel will try to send an ARP request asking "Who has 192.168.50.100?". Because the client has not assigned that IP yet, it ignores the ARP request and the packet never leaves your server.
- **The Broadcast Flag (0x8000)**: Earlier, we set `dhcp->flags = htons(0x8000)` in your client's DHCPDISCOVER. According to RFC 2131, whenever this broadcast bit is set, the DHCP server must send its DHCPOFFER and DHCPACK to 255.255.255.255:68.

### Umm do not I need to complete packet: Eth + IP + UDP + DHCP from DHCP server ??
- No, on the server side you do NOT build Eth + IP + UDP headers manually—you only pass the struct dhcp_packet directly to sendto().
- Why Your Client Needed All 4 Headers, While Your Server Only Needs 1
    - Your DHCP Client
        - AF_PACKET sits at Layer 2 (just like libpcap does under the hood), so you build all 4 headers on sendto(), and when recvfrom() captures the server's DHCPOFFER, you receive all 4 headers back and skip the first 42 bytes (14 + 20 + 8) to read struct dhcp_packet.
    - Your DHCP server
        - Because SOCK_DGRAM is a Layer 4 socket, the Linux kernel automatically wraps your struct dhcp_packet inside UDP (port 67 → 68), IP (192.168.50.1 → 255.255.255.255), and Ethernet headers.

    - `AF_PACKET (SOCK_RAW) = You handle all 4 layers (Eth + IP + UDP + DHCP).`
    - `AF_INET (SOCK_DGRAM) = You handle only 1 layer (DHCP payload); the kernel handles the rest.`

### Why do I need the below part in server in sendto??
```c

// Target 255.255.255.255:68 (DHCP Client Port)
struct sockaddr_in broadcast_addr;
memset(&broadcast_addr, 0, sizeof(broadcast_addr));
broadcast_addr.sin_family      = AF_INET;
broadcast_addr.sin_port        = htons(68);
broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

```
- You need that struct sockaddr_in broadcast_addr block because UDP (SOCK_DGRAM) is connectionless—unlike TCP, your server socket does not remember who it is talking to, so you must explicitly tell the Linux kernel which destination IP address and UDP port to stamp onto every outgoing packet.
- When you pass &broadcast_addr into sendto(), the kernel uses those three fields to build the outer IPv4 and UDP headers around your struct dhcp_packet:
    - `broadcast_addr.sin_family = AF_INET`; Tells the kernel to build a standard IPv4 header around your data.
    - `broadcast_addr.sin_port = htons(68);` Sets the Destination UDP Port to 68 (the standard port where all DHCP clients listen for server replies). htons() converts 68 into Big-Endian network byte order.
    - `broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255")`: Sets the Destination IPv4 Address to 255.255.255.255 (Limited Broadcast).

- Why You Can't Just Reuse client_addr from recvfrom()?
    - In a normal UDP echo server, you usually pass the client_addr struct filled by recvfrom() straight back into sendto(). For DHCP, that breaks completely:
    1. **Client Had No IP Yet**: When recvfrom() captured the DHCPDISCOVER, the client's Source IP was 0.0.0.0. If you passed client_addr back to sendto(), the kernel would try to send your DHCPOFFER to 0.0.0.0, which fails immediately.
    2. **Client Might Have Used an Ephemeral Source Port**: If your custom raw client sent DHCPDISCOVER from a random source port instead of port 68, reusing client_addr would send the reply to the wrong port. Setting htons(68) guarantees the reply always lands on the official DHCP client port (68).

### But where is the client mac's address so that if multiple clients are listening, then correct client receives this??
- When broadcasting a DHCPOFFER to 255.255.255.255:68, every client on the local network receives the packet at the hardware level, and each client filters it in software by checking two fields inside your struct dhcp_packet payload: chaddr (Client MAC Address) and xid (Transaction ID).
- What If You Want Unicast Delivery to Only One MAC?
    - If a client sends DHCPDISCOVER without the Broadcast flag (flags == 0x0000), a production server avoids broadcasting to everyone by opening a raw AF_PACKET socket and setting:
        - **Ethernet Destination MAC (eth->h_dest)**: discover->chaddr (the specific client's MAC address, instead of FF:FF:FF:FF:FF:FF).
        - **IPv4 Destination IP (ip->daddr)**: offered_ip (yiaddr).

### Debug anything via gdb
- how do I attach gdb to the client process and debug the things by setting the breakpoint?
- Compile with Debug Symbols
    - `gcc -g -O0 dhcp_client.c -o dhcp_client`
- Find the Process ID (PID) and Attach GDB
    - pgrep dhcp_client
    - sudo gdb -p <PID>
- Or if client is not long running program:
    - `sudo ip netns exec dhcp_client_ns gdb --args ./client veth_client`
- THen use normal gdb commands:
    - break parse_dhcp_offer # Break at a function name
    - break dhcp_client.c:85 # Or break at a specific file and line number
    - continue # Resume the process so it waits for incoming packets
- Check parsed struct values:
    - (gdb) print *eth
    - (gdb) print *ip
    - (gdb) print *udp
    - (gdb) print *dhcp
- Dump the first 64 bytes of buffer in hex
    - (gdb) x/64bx buffer
- Other useful commands to check the individual values
    - p ntohs($5.uh_sport)
    - p ntohs($5.uh_dport)
    - p ntohs($5.uh_ulen)
