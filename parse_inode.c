// Before running this make sure your linux has ext2fs header file, if not: sudo apt update && sudo apt install comerr-dev libext2fs-dev
// gcc parse_inode.c -o parse_inode -lext2fs -lcom_err

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ext2fs/ext2fs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define EXT4_N_BLOCKS 15
#define BUF_LEN 4096

typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint16_t __u16;
typedef uint32_t __u32;

// copied from https://github.com/torvalds/linux/blob/fdf0eaf11452d72945af31804e2a1048ee1b574c/fs/ext4/ext4.h#L769
struct ext4_inode
{
    __le16 i_mode;
    __le16 i_uid;
    __le32 i_size_lo;
    __le32 i_atime;
    __le32 i_ctime;
    __le32 i_mtime;
    __le32 i_dtime;
    __le16 i_gid;
    __le16 i_links_count;
    __le32 i_blocks_lo;
    __le32 i_flags;
    union
    {
        struct
        {
            __le32 l_i_version;
        } linux1;
        struct
        {
            __u32 h_i_translator;
        } hurd1;
        struct
        {
            __u32 m_i_reserved1;
        } masix1;
    } osd1;
    __le32 i_block[EXT4_N_BLOCKS];
    __le32 i_generation;
    __le32 i_file_acl_lo;
    __le32 i_size_high;
    __le32 i_obso_faddr;
    union
    {
        struct
        {
            __le16 l_i_blocks_high;
            __le16 l_i_file_acl_high;
            __le16 l_i_uid_high;
            __le16 l_i_gid_high;
            __le16 l_i_checksum_lo;
            __le16 l_i_reserved;
        } linux2;
        struct
        {
            __le16 h_i_reserved1;
            __u16 h_i_mode_high;
            __u16 h_i_uid_high;
            __u16 h_i_gid_high;
            __u32 h_i_author;
        } hurd2;
        struct
        {
            __le16 h_i_reserved1;
            __le16 m_i_file_acl_high;
            __u32 m_i_reserved2[2];
        } masix2;
    } osd2;
    __le16 i_extra_isize;
    __le16 i_checksum_hi;
    __le32 i_ctime_extra;
    __le32 i_mtime_extra;
    __le32 i_atime_extra;
    __le32 i_crtime;
    __le32 i_crtime_extra;
    __le32 i_version_hi;
    __le32 i_projid;
} __attribute__((__packed__));

/*
Scans through the actual inode table of the filesystem, seeing if any of their
    checksums match the checksum of the candidate inode.
Returns 0 if no inodes match.
Returns the inode number if an inode matches.
*/
int get_inode_num(struct ext4_inode *inode, ext2_filsys fs)
{
    ext2_ino_t ino;
    struct ext2_inode ext2_inode;

    for (ino = 1; ino <= fs->super->s_inodes_count; ++ino)
    {
        if (ext2fs_read_inode(fs, ino, &ext2_inode))
        {
            fprintf(stderr, "Error reading inode\n");
            return 0;
        }

        if (ext2_inode.osd2.linux2.l_i_checksum_lo == inode->osd2.linux2.l_i_checksum_lo)
        {
            return ino;
        }
    }
    return 0;
}

void print_inode_info(struct ext4_inode *inode, int ino)
{
    printf("Inode: %d   Mode:  %04o\n", ino, inode->i_mode & 0xfff);
    printf("User:  %u   Group:  %u   Size: %u\n", inode->i_uid, inode->i_gid, inode->i_size_lo);
    printf("Links: %u   Blockcount: %u\n", inode->i_links_count, inode->i_blocks_lo);
    printf("Inode checksum: 0x0%x\n", inode->osd2.linux2.l_i_checksum_lo + (inode->i_checksum_hi << 16));
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <disk file> <inode_number>\n", argv[0]);
        return 1;
    }

    ext2_filsys fs;
    // Open the filesystem via the library (natively handles LVM maps!)
    if (ext2fs_open(argv[1], 0, 0, 0, unix_io_manager, &fs))
    {
        fprintf(stderr, "Error opening filesystem\n");
        return 1;
    }

    // Convert the argument into a proper ext2 inode integer type
    ext2_ino_t target_ino = strtoul(argv[2], NULL, 0);

    // Create the library's internal structure buffer
    struct ext2_inode disk_inode;

    // Grab the true inode metadata cleanly from the system
    if (ext2fs_read_inode(fs, target_ino, &disk_inode))
    {
        fprintf(stderr, "Error: Could not read Inode %u from the filesystem.\n", target_ino);
        ext2fs_close(fs);
        return 1;
    }

    // Safely cast or map fields to read the values
    printf("--- SUCCESS! Inode %u Located ---\n", target_ino);
    printf("Mode:        %04o\n", disk_inode.i_mode & 0xfff);
    printf("User ID:     %u\n", disk_inode.i_uid);
    printf("Group ID:    %u\n", disk_inode.i_gid);
    printf("File Size:   %u bytes\n", disk_inode.i_size);
    printf("Links Count: %u\n", disk_inode.i_links_count);
    printf("Blocks Count:%u\n", disk_inode.i_blocks);
    printf("Checksum Lo: 0x%04x\n", disk_inode.osd2.linux2.l_i_checksum_lo);

    ext2fs_close(fs);
    return 0;
}
