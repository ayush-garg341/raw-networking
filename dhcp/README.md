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


