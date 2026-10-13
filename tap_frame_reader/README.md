### TUN vs TAP
- TUN operates at Layer 3 (Network Layer) handling IP packets.
- TAP operates at Layer 2 (Data Link Layer) handling raw Ethernet frames.

- TUN ( Network Tunnel )
    - OSI Layer: Layer 3 (Network Layer)
    - Data Unit: IP packets (IPv4 / IPv6)
    - Behavior: Acts as a point-to-point link (like a direct serial connection).
    - Broadcast Traffic: Does not pass broadcast or multicast traffic.
    - Overhead: Lower overhead (no 14-byte Ethernet header).
    - Common Uses: Standard VPNs (like OpenVPN or WireGuard), remote access, and cloud routing

- TAP ( Network TAP )
    - OSI Layer: Layer 2 (Data Link Layer)
    - Data Unit: Ethernet frames (contains MAC addresses)
    - Behavior: Acts as a virtual Ethernet adapter (like a virtual network switch).
    - Broadcast Traffic: Supports broadcast, multicast, and non-IP protocols (like DHCP, IPX, or NetBIOS).
    - Overhead: Higher overhead due to Ethernet header encapsulation.
    - Common Uses: Local Area Network (LAN) bridging, virtual machine emulation (like KVM/QEMU), and legacy apps.

- The Linux driver /dev/net/tun supports two modes:
    - TUN (IFF_TUN)
    - TAP (IFF_TAP)

- The IFF_NO_PI Flag (Crucial Trap)
    - By default, the Linux tun/tap driver prepends a 4-byte "Packet Information" (PI) header (flags + proto) before the frame data.
    - To get clean, raw Ethernet frames starting at byte 0, you must tell the kernel to omit this by setting the flag:
    IFF_TAP | IFF_NO_PI

- How the Handshake Works with the Kernel
    - Attaching to a TAP device follows a standard Unix system-call pattern:
    - Open the character device: open("/dev/net/tun", O_RDWR)
    - Prepare a struct ifreq (defined in <net/if.h> and <linux/if_tun.h>):
        - Zero it out (memset).
        - Set ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
        - Optionally set strncpy(ifr.ifr_name, "tap0", IFNAMSIZ); to name it explicitly.
    - Register the interface using ioctl(fd, TUNSETIFF, (void *)&ifr).
    - The file descriptor fd is now your pipe to Layer 2. Calling read(fd, buffer, sizeof(buffer)) will block until the host OS pushes an Ethernet frame into the interface.

- Configure the interface once your program creates it
```
# If creating persistently:
sudo ip tuntap add mode tap dev tap0
sudo ip addr add 192.168.42.1/24 dev tap0
sudo ip link set dev tap0 up
```

### what is struct ifreq defined in <net/if.h> and <linux/if_tun.h>?
- The struct ifreq (interface request) structure is a C data structure used in Linux to get and set network interface configuration parameters (such as IP addresses, MTU, flags, or hardware addresses) via ioctl system calls.

### what is ioctl?
- ioctl (short for Input/Output Control) is a system call in Unix-like operating systems used to manipulate the underlying device parameters of special files. While standard operations like read() and write() handle raw byte streams, ioctl handles out-of-band control operations—such as changing network flags, querying hardware details, adjusting terminal windows, or ejecting a CD-ROM.

### what is /dev/net/tun, is this built in, what does it mean??
- /dev/net/tun is a special character device file in Linux that acts as the management gateway for creating and controlling virtual network interfaces, specifically TUN (Network Layer / IP) and TAP (Data Link Layer / Ethernet) adapters.
- Unlike standard hardware device files (like /dev/sda for a hard drive), /dev/net/tun does not represent a physical piece of hardware. Instead, it is a software hook provided by the Linux kernel.
- Yes, it is built directly into almost all modern Linux kernels. It is provided by the kernel module named tun. In almost every modern Linux distribution (Ubuntu, Debian, Fedora, Arch, CentOS), this module is either compiled directly into the kernel core or is configured to load automatically on demand.
`ls -l /dev/net/tun`
- To understand what /dev/net/tun means, you have to look at how a standard network application works compared to a Virtual Private Network (VPN) or simulation program.
- Normally, when an app sends data, it goes to the kernel, which pushes it out of a physical wire via a hardware network card (like eth0 or wlan0).
- The TUN/TAP Flow (What /dev/net/tun enables):
    - /dev/net/tun allows a user-space program (like OpenVPN, WireGuard, or an SSH tunnel) to act like a piece of network hardware.
    - When a program opens /dev/net/tun and configures it via ioctl, the kernel magically creates a new virtual network adapter (like tun0).
    - The App sends data: When your browser sends traffic through tun0, the Linux kernel doesn't look for a physical wire. Instead, it redirects those raw network packets straight into the file descriptor of the user-space program that opened /dev/net/tun.
    - The VPN intercepts it: The VPN program reads the raw packets, encrypts them, wraps them in a new outer layer, and sends them out over the real internet using your physical eth0 or Wi-Fi card.

### What this command will do? `ioctl(fd, TUNSETIFF, (void *)&ifr)`
- This specific command binds a network application to a virtual TUN or TAP network interface inside the Linux kernel.
    - fd: This must be an open file descriptor pointing specifically to /dev/net/tun.
    - TUNSETIFF: This is a specific kernel command macro that stands for "TUN Set Interface Flags". It tells the kernel module: "I want to register, create, or attach to a virtual interface right now."
    - (void *)&ifr: This passes the address of your struct ifreq variable. Before calling this, you must have populated ifr with two critical pieces of information:
        - ifr.ifr_name: The name you want for the interface (e.g., "tun0", "tap1"). If you leave this blank (""), the kernel will automatically assign the next available name (like tun0).
        - ifr.ifr_flags: The mode of the interface. Typically IFF_TUN (for layer 3 IP packets) or IFF_TAP (for layer 2 Ethernet frames), often combined with IFF_NO_PI (to omit extra packet information headers).

- What the Kernel Does Behind the Scenes?
    - When this ioctl executes, the Linux kernel checks the interface name you requested:
        - If the interface (e.g., tun0) does not exist yet: The kernel instantly creates a brand new virtual network interface in the operating system. If you run the ip link or ifconfig command in your terminal while your program is running, you will see tun0 pop up in the list.
        - If the interface already exists: The kernel attaches your current file descriptor (fd) to that existing interface.

    - The moment this command succeeds (returns 0), it changes how the file descriptor fd behaves:
        - From this point forward, fd is no longer just a management handle. It is now a live pipeline to the network interface.
        - Any traffic the operating system or other apps send through the tun0 virtual network adapter can now be grabbed by your program using a standard read(fd, ...).
        - Any raw network packets your program constructs and sends via write(fd, ...) will look to the Linux kernel as if they were physically received from an external network cable.

    - Lifespan and Cleanup
        - This virtual interface is closely tied to your file descriptor. By default, the moment your program exits or you call close(fd), the virtual tun0 interface is automatically destroyed and deleted by the kernel.

### Umm you mentioned: get and set network interface configuration parameters. What does configuration parameter mean, can you give me examples to clear my understadning?
- In networking, configuration parameters are the settings, rules, and identities assigned to a network interface (like your Wi-Fi, Ethernet card, or a virtual TUN device) so that the operating system and the network know how to handle it.
- Think of a network interface like a cell phone. The "hardware" is the phone itself, but the configuration parameters are things like your phone number, your data limit, and whether your phone is currently in airplane mode.
- Here is a grouped list of the most common configuration parameters you can get or set using struct ifreq and ioctl:
    - Identity & Addressing Parameters
        - IP Address (ifr_addr): The unique software address assigned to the interface (e.g., 192.168.1.50).
        - Netmask / Subnet Mask (ifr_netmask): Defines the size of the local network network (e.g., 255.255.255.0).
        - MAC / Hardware Address (ifr_hwaddr): The physical, hardcoded address of the network card (e.g., 00:1a:2b:3c:4d:5e). You can actually use ioctl to spoof or change this.
        - Broadcast Address (ifr_broadaddr): The special address used to send data to everyone on the local network simultaneously.
    - Operational Status (Flags)
        - These parameters use binary switches (flags stored in ifr_flags) to turn interface features on or off.
        - Interface Up/Down (IFF_UP): Is the interface active? Setting this to "Up" activates it (like turning on Wi-Fi). Setting it to "Down" disables it.
        - Promiscuous Mode (IFF_PROMISC): When turned on, it tells the network card to capture every single packet passing by the wire, even if it's addressed to someone else. This is what packet sniffers like Wireshark use.
        - Multicast Support (IFF_MULTICAST): Enables or disables the ability to receive data streams sent to specific groups of computers (common in video streaming).
    -  Performance & Routing Parameters
        - MTU (ifr_mtu): Maximum Transmission Unit. This is the maximum size of a single packet (in bytes) that the interface can handle at one time. The standard is usually 1500 bytes. If you are on a poor connection, you might lower this parameter to prevent data corruption.
        - Interface Index (ifr_ifindex): A unique number the Linux kernel assigns to an interface internally (e.g., eth0 might be index 2). Many advanced networking functions require this number instead of the text name.
        - Metric (ifr_metric): A priority number used by routing tables. If your computer is connected to both Wi-Fi and Ethernet, the operating system looks at this parameter to decide which interface is "cheaper" or faster to use.

- When you run a command like ip addr add 192.168.1.10/24 dev eth0 or ifconfig eth0 mtu 1400 in your Linux terminal, those command-line tools are acting as user space programs. Under the hood, they take your input, package those configuration parameters into a struct ifreq, and fire them into the kernel using ioctl.

### What is the purpose of ifreq and ioctl, are they replacing creating virtual ethernet or replacing adding ip addr on veth or what they are doing and their purpose?
- No, struct ifreq and ioctl are not replacing virtual Ethernets (veth) or commands like ip addr. In fact, they are the engine inside them.
- When you run a command like `ip link add veth0 type veth` or `ip addr add 192.168.1.1/24 dev veth0`, those commands are high-level user tools. Under the hood, those exact tools package your commands into data structures (like struct ifreq) and use system calls (like ioctl or Netlink) to talk to the Linux kernel.
- What are they actually doing in practice?
    - Controlling Virtual Interfaces (Like TUN/TAP)
        - If you are writing a VPN application (like OpenVPN), you can't just expect a physical eth0 network card to magically route encrypted traffic. Your program needs its own dedicated entryway into the kernel.
        - By calling `ioctl(fd, TUNSETIFF, &ifr)`, your program tells the kernel: "Hey, dynamically create a virtual interface called tun0 right now and hand its data stream directly to my process."
    - Managing Existing Interfaces (Like veth, eth0, wlan0)
        - If you already created a virtual ethernet pair (veth0 and veth1) using the command line, you might want a custom program to monitor or alter it without invoking slow bash commands.
        - To check its health: You pass SIOCGIFFLAGS via ioctl to see if veth0 is currently up or down.
        - To configure it: You pass SIOCSIFADDR via ioctl to assign an IP address to veth0 programmatically.

### Tell me more about the flags like: SIOCSIFFLAGS, SIOCSIFMTU, TUNSETIFF etc.
- These uppercase words like SIOCSIFFLAGS, SIOCSIFMTU, and TUNSETIFF are Request Codes (often called ioctl commands or request flags).
- In C, they are numeric constants defined as macros in kernel headers (like <sys/ioctl.h>, <linux/sockios.h>, and <linux/if_tun.h>). When you pass one to ioctl(), it tells the kernel driver exactly what action you want to perform and which field of the struct ifreq union to look at.
- The standard network ioctl commands follow a highly predictable naming pattern that makes them easy to read:
    - SIOC: Socket I/O Control
    - G or S: Get (Read from kernel) or **S**et (Write to kernel)
    - IF: Interface
    - FLAGS / MTU / ADDR: The specific configuration parameter.
- SIOCGIFFLAGS & SIOCSIFFLAGS (Get / Set Interface Flags)
    - What they target: The ifr_flags field in struct ifreq.
    - Purpose: They change the administrative state and operational modes of the interface using bitwise flags like IFF_UP, IFF_PROMISC, or IFF_LOOPBACK.
    - How it's used: To turn a network card on programmatically, you first Get the flags, use a bitwise OR (|) to add the IFF_UP flag, and then Set it back:

    ```C
    ioctl(fd, SIOCGIFFLAGS, &ifr);        // 1. Read existing flags
    ifr.ifr_flags |= (IFF_UP | IFF_RUNNING); // 2. Turn on the "UP" switch
    ioctl(fd, SIOCSIFFLAGS, &ifr);        // 3. Write them back to kernel
    ```
- SIOCGIFMTU & SIOCSIFMTU (Get / Set Maximum Transmission Unit)
    - What they target: The ifr_mtu field in struct ifreq.
    - Purpose: Reads or changes the maximum packet size (in bytes) allowed through the wire.
    - How it's used: If your application is dropping packets on a congested network, you might use this to force the MTU down from 1500 to 1400 bytes to minimize fragmentation.

- SIOCGIFADDR & SIOCSIFADDR (Get / Set Interface Address)
    - What they target: The ifr_addr field (a struct sockaddr structure).
    - Purpose: Queries or sets the IPv4 address of the network interface.
    - How it's used: Used by setup utilities to assign an IP address (like 192.168.1.1) directly to a new interface from within code.

-  TUN/TAP Specific Commands (Virtual Drivers)
    - TUNSETIFF (TUN Set Interface)
        - What it targets: The ifr_name and ifr_flags fields.
        - Purpose: This is the most crucial command when dealing with virtual networking. It registers, binds, or dynamically spawns a virtual TUN (IFF_TUN) or TAP (IFF_TAP) device interface in the OS.
        - How it's used: You call it exactly once right after opening /dev/net/tun to hook your application up to the kernel's virtual network pipe.
    - TUNSETOWNER & TUNSETGROUP (TUN Set Owner / Group)
        - Purpose: Grants ownership of a specific virtual interface to a non-root user ID (UID) or group ID (GID).
        - Why it matters: By default, creating and interacting with virtual network interfaces requires root (sudo) privileges. By passing a user ID to ioctl(fd, TUNSETOWNER, uid), you allow a standard user-space program to read and write network frames from that device without running the entire application as root (improving security).
    - TUNSETQUEUE (Multi-Queue Support)
        - Purpose: Attaches or detaches additional file descriptors to an existing virtual network interface.
        - Why it matters: On high-performance systems with many CPU cores, a single file descriptor loop can become a bottleneck for network traffic. TUNSETQUEUE allows an application to open /dev/net/tun multiple times and attach each file descriptor to the same virtual card, enabling multi-threaded parallel packet processing.

- Can we add ip addr and then set the link UP, programmatically as well??
    - Yes, you can completely configure and activate the interface programmatically, eliminating the need for commands like ip addr add or ip link set up in the terminal.
    - To achieve this, you need to perform two separate ioctl operations after creating your TAP or TUN interface:
        - Set the IP Address using SIOCSIFADDR
        - Set the Link Flags (UP and RUNNING) using SIOCSIFFLAGS

### One thing I am confused on, I see the tap0 is 192.168.42.1 but didn't assign the IP to user space program, how did it get the ping intended for 192.168.42.2?
- This is the biggest "Aha!" moment of the entire project.
- The Linux kernel does not assign the IP address 192.168.42.2 to your user-space program. In fact, the Linux kernel has absolutely no idea that your C program exists, nor does it know that 192.168.42.2 is actually running inside your terminal.
- You simply hardcode 192.168.42.2 as a variable in your C code, and you use the Address Resolution Protocol (ARP) to trick the Ubuntu host into believing a real machine is sitting on the other end of the cable.
- When you run ping 192.168.42.2 from the Ubuntu terminal, the Ubuntu kernel looks at its routing table. It sees that the 192.168.42.0/24 subnet belongs to tap0 (IP: 192.168.42.1).
- The ARP Request (The Broadcast): Before Ubuntu can send the ICMP Ping packet, it needs to wrap it in an Ethernet frame. To do that, it needs a Destination MAC Address. Because it doesn't know the MAC address for .2, it pauses the ping and sends an ARP Request out the tap0 interface. This request literally says: "Broadcast to everyone: Who has 192.168.42.2? Tell 192.168.42.1."
- Your C Program Reads the Frame: That ARP request flows down the virtual cable and pops out of your read(tap_fd, ...) loop as a raw array of hex bytes.

