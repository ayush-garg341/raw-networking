// sudo apt update && sudo apt install libpcap-dev
// compile with gcc -lpcap option if using pcap header file.

#include<sys/types.h>
#include<sys/socket.h>
#include<sys/ioctl.h>
#include<net/ethernet.h>
#include<linux/if_packet.h>
#include<arpa/inet.h>
#include<net/if.h>
#ifndef __linux__
#include<net/if_dl.h>
#endif

#include<netinet/if_ether.h> // struct ethhdr
#include<netinet/in.h> // Defines Internet address family structures like sockaddr_in (IPv4) and sockaddr_in6 (IPv6), alongside protocol constants (IPPROTO_TCP, IPPROTO_UDP) and byte-order conversion utilities (htonl, htons)
#include<netinet/ip.h> // Provide low-level IPv4 and IPv6 packet header structures for raw socket development.

#define __FAVOR_BSD
#include<netinet/udp.h> // Defines the udphdr structure for inspecting or building raw UDP datagrams.
#include<stdio.h>
#include<stdint.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<errno.h>
#include<ifaddrs.h>
#include<sys/random.h>
#include"common.h"


/*
 * Generate random 32 bit transaction ID
 */
uint32_t generate_xid(void) {
    uint32_t xid = 0;
    if (getrandom(&xid, sizeof(xid), 0) != sizeof(xid)) {
        perror("getrandom failed");
        return 0x12345678; // Fallback
    }
    return xid;
}


/*
 * Get MAC address of given link(dev_name)
 */
static int get_mac_address(char *dev_name, u_int8_t *mac) {
    struct ifreq s;
    int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    int result;

    // destination, source
    strcpy(s.ifr_name, dev_name);

    // system call that lets user-space programs communicate directly with kernel device drivers to perform operations not covered by standard file calls like configuring hardware registers, setting serial port baud rates, or querying terminal window sizes.
    result = ioctl(fd, SIOCGIFHWADDR, &s);
    close(fd);
    if (result != 0)
        return -1;

    memcpy((void *)mac, s.ifr_addr.sa_data, 6);
    return 0;

}

// Standard 16-bit one's complement IPv4 header checksum
uint16_t calculate_ip_checksum(void *vdata, size_t length) {
    uint16_t *data = (uint16_t *)vdata;
    uint32_t sum = 0;

    for (size_t i = 0; i < length / 2; i++) {
        sum += ntohs(data[i]);
    }
    if (length % 2 == 1) {
        sum += ((uint8_t *)vdata)[length - 1] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return htons((uint16_t)(~sum));
}

size_t build_dhcp_discover(uint8_t *buffer, const uint8_t *client_mac, uint32_t xid) {
    // fills the first 1,500 bytes of memory pointed to by buffer with zeros.
    memset(buffer, 0, 1500);

    // Pointers to each layer inside the single buffer
    struct ethhdr *eth  = (struct ethhdr *)buffer;
    struct iphdr  *ip   = (struct iphdr *)(buffer + sizeof(struct ethhdr));
    struct udphdr *udp  = (struct udphdr *)(buffer + sizeof(struct ethhdr) + sizeof(struct iphdr));
    dhcp_t *dhcp = (dhcp_t *)(buffer + sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr));

    // ---------------------------------------------------------
    // 1. DHCP PAYLOAD (300 Bytes)
    // ---------------------------------------------------------
    dhcp->opcode           = 1;              // 1 = BOOTREQUEST
    dhcp->htype        = 1;              // 1 = Ethernet
    dhcp->hlen         = 6;              // MAC address length
    dhcp->hops         = 0;
    dhcp->xid          = htonl(xid);     // Random Transaction ID
    dhcp->secs         = 0;
    dhcp->flags        = htons(0x8000);  // Broadcast flag (tells server to broadcast reply)
    dhcp->ciaddr       = 0;
    dhcp->yiaddr       = 0;
    dhcp->siaddr       = 0;
    dhcp->giaddr       = 0;
    memcpy(dhcp->chaddr, client_mac, 6); // Client MAC inside DHCP payload
    dhcp->magic_cookie = htonl(DHCP_MAGIC_COOKIE);

    // DHCP TLV Options
    int opt = 0;
    // Option 53: DHCP Message Type = 1 (DHCPDISCOVER)
    dhcp->options[opt++] = 53;
    dhcp->options[opt++] = 1;
    dhcp->options[opt++] = 1;

    // Option 55: Parameter Request List (Subnet Mask, Router, DNS)
    dhcp->options[opt++] = 55;
    dhcp->options[opt++] = 3;
    dhcp->options[opt++] = 1;  // Subnet Mask
    dhcp->options[opt++] = 3;  // Default Gateway
    dhcp->options[opt++] = 6;  // DNS Server

    // Option 255: End Option
    dhcp->options[opt++] = 255;

    size_t dhcp_len = sizeof(struct dhcp) + opt;
    size_t udp_len = sizeof(struct udphdr) + dhcp_len;
    size_t ip_len = sizeof(struct iphdr) + udp_len;
    size_t total_len = sizeof(struct ethhdr) + ip_len;

    // ---------------------------------------------------------
    // 2. UDP HEADER (8 Bytes)
    // ---------------------------------------------------------
    udp->source = htons(68);             // DHCP Client Port
    udp->dest   = htons(67);             // DHCP Server Port
    udp->len    = htons(udp_len);        // UDP header + DHCP payload
    udp->check  = 0;                     // Optional in IPv4 (0 = disabled, avoids pseudo-header math)

    // ---------------------------------------------------------
    // 3. IPv4 HEADER (20 Bytes)
    // ---------------------------------------------------------
    ip->ihl      = 5;                    // 5 * 32-bit words = 20 bytes
    ip->version  = 4;                    // IPv4
    ip->tos      = 0;
    ip->tot_len  = htons(ip_len);        // IP header + UDP header + DHCP payload
    ip->id       = htons(0x1234);        // Identification
    ip->frag_off = 0;
    ip->ttl      = 64;                   // Standard Time-To-Live
    ip->protocol = IPPROTO_UDP;          // Next layer is UDP (17)
    ip->saddr    = inet_addr("0.0.0.0");         // Client has no IP yet
    ip->daddr    = inet_addr("255.255.255.255"); // Limited Broadcast
    ip->check    = 0;                    // Must be 0 before calculating checksum
    ip->check    = calculate_ip_checksum(ip, sizeof(struct iphdr));

    // ---------------------------------------------------------
    // 4. ETHERNET HEADER (14 Bytes)
    // ---------------------------------------------------------
    memset(eth->h_dest, 0xFF, 6);        // Broadcast MAC: FF:FF:FF:FF:FF:FF
    memcpy(eth->h_source, client_mac, 6);// Client's MAC address
    eth->h_proto = htons(ETH_P_IP);      // Next layer is IPv4 (0x0800)

    return total_len;
}


int main(int argc, char* argv[]) {

    int result;
    char *dev;
    u_int8_t mac[6];

    if (argc < 2 || (strcmp(argv[1], "-h") == 0)) {
        printf("Usage: %s <interface>\n", argv[0]);
        return 0;
    }

    dev = argv[1];

    /* Get the MAC address of the interface */
    result = get_mac_address(dev, mac);

    if (result != 0)
    {
        PRINT(VERBOSE_LEVEL_ERROR, "Unable to get MAC address for %s", dev);
        return -1;
    }
    PRINT(VERBOSE_LEVEL_INFO, "%s MAC : %02X:%02X:%02X:%02X:%02X:%02X",
          dev, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // 1. Create a Layer 2 raw socket, AF_PACKET and SOCK_RAW
    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock < 0) {
        perror("socket failed (run with sudo)");
        return 1;
    }

    // 2. Look up the interface index for veth_client
    unsigned int if_index = if_nametoindex(dev);
    if (if_index == 0) {
        perror("if_nametoindex failed");
        close(sock);
        return 1;
    }

    PRINT(VERBOSE_LEVEL_INFO, "Interface index is %d\n", if_index);

    // 3. Bind the raw socket exclusively to veth_client
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family   = AF_PACKET;
    sll.sll_ifindex  = if_index;
    sll.sll_protocol = htons(ETH_P_ALL);

    if (bind(sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        perror("bind failed");
        close(sock);
        return 1;
    }

    // 4. Build the packet and save its exact byte length
    u_int8_t *buffer = malloc(1500 * sizeof(char));
    uint32_t xid = generate_xid();
    size_t packet_len = build_dhcp_discover(buffer, mac, xid);
    PRINT(VERBOSE_LEVEL_INFO, "Total packet len is: %ld\n", packet_len);

    // 5. Send the raw Ethernet frame out of veth_client
    ssize_t sent = sendto(sock, buffer, packet_len, 0, (struct sockaddr *)&sll, sizeof(sll));
    if (sent < 0) {
        perror("sendto failed");
        free(buffer);
        close(sock);
        return 1;
    }

    PRINT(VERBOSE_LEVEL_INFO, "Sent DHCPDISCOVER (%zd bytes) via %s (XID: 0x%08x)\n", sent, dev, xid);

    free(buffer);
    close(sock);
    return 0;
}
