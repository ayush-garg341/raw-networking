#ifndef COMMON_H
#define COMMON_H

#include <stdint.h> // Recommended for fixed-width types like int32_t
#include<unistd.h>
#include<net/ethernet.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/ioctl.h>
#include<linux/if_packet.h>
#include<arpa/inet.h>
#include<net/if.h>

typedef u_int32_t ip4_t;

#define DHCP_CHADDR_LEN 16
#define DHCP_SNAME_LEN  64
#define DHCP_FILE_LEN   128

#pragma pack(push, 1)
typedef struct dhcp {
    u_int8_t    opcode;
    u_int8_t    htype;
    u_int8_t    hlen;
    u_int8_t    hops;
    u_int32_t   xid;
    u_int16_t   secs;
    u_int16_t   flags;
    ip4_t       ciaddr;
    ip4_t       yiaddr;
    ip4_t       siaddr;
    ip4_t       giaddr;
    u_int8_t    chaddr[DHCP_CHADDR_LEN];
    char        bp_sname[DHCP_SNAME_LEN];
    char        bp_file[DHCP_FILE_LEN];
    uint32_t    magic_cookie;
    u_int8_t    options[0];
} dhcp_t;
#pragma pack(pop)

#define DHCP_BOOTREQUEST                    1
#define DHCP_BOOTREPLY                      2

#define DHCP_HARDWARE_TYPE_10_EHTHERNET     1

#define DHCP_OPTION_DISCOVER                1
#define DHCP_OPTION_OFFER                   2
#define DHCP_OPTION_REQUEST                 3
#define DHCP_OPTION_PACK                    4

#define DHCP_CLIENT_PORT    68

#define DHCP_MAGIC_COOKIE   0x63825363

typedef enum {
    VERBOSE_LEVEL_NONE,
    VERBOSE_LEVEL_ERROR,
    VERBOSE_LEVEL_INFO,
    VERBOSE_LEVEL_DEBUG,
} verbose_level_t;

#define PRINT(verbose_level, fmt, args...)                              \
do{                                                                     \
    if( verbose_level <= program_verbose_level ) {                      \
        if ( verbose_level == VERBOSE_LEVEL_DEBUG ) {                   \
            printf("%s:%d:%s::",    __FILE__, __LINE__, __FUNCTION__);  \
        }                                                               \
        printf(fmt, ##args);                                            \
        printf("\n");                                                   \
    }                                                                   \
}while(0)

verbose_level_t program_verbose_level = VERBOSE_LEVEL_DEBUG;

#endif
