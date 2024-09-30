/*
 * luxOS - a unix-like operating system 
 * Omar Elghoul, 2024
 *
 * lumen: router enabling communication between the kernel and the servers
 */

/* This layer relays syscall requests to the appropriate servers */
/* File system requests get redirected to the virtual file system, and someday
 * network requests will get redirected to the networking layer */

#include <sys/socket.h>
#include <liblux/liblux.h>
#include <lumen/lumen.h>

static int syscallRelayTable[];

void relaySyscallRequest(SyscallHeader *hdr) {
    uint16_t command = hdr->header.command & 0x7FFF;    // clear bit 15
    int socket;

    switch(syscallRelayTable[command]) {
    case RELAY_VFS:
        //luxLog(KPRINT_LEVEL_DEBUG, "relaying to vfs...\n");
        socket = vfs;
        break;
    case RELAY_KTHD:
        socket = kthd;
        break;
    default:
        luxLogf(KPRINT_LEVEL_WARNING, "unhandled syscall command 0x%X, relay to %d\n", command|0x8000, syscallRelayTable[command]);
        return;
    }

    send(socket, hdr, hdr->header.length, 0);
}

static int syscallRelayTable[] = {
    RELAY_VFS,          // 0 - stat()
    RELAY_VFS,          // 1 - flush()
    RELAY_VFS,          // 2 - mount()
    RELAY_VFS,          // 3 - umount()
    RELAY_VFS,          // 4 - open()
    RELAY_VFS,          // 5 - read()
    RELAY_VFS,          // 6 - write()
    RELAY_VFS,          // 7 - ioctl()
    RELAY_VFS,          // 8 - opendir()
    RELAY_VFS,          // 9 - readdir()
    RELAY_VFS,          // 10 - chmod()
    RELAY_VFS,          // 11 - chown()
    RELAY_VFS,          // 12 - link()
    RELAY_VFS,          // 13 - mkdir()
    RELAY_VFS,          // 14 - rmdir()

    RELAY_KTHD,         // 15 - exec() family
    RELAY_KTHD,         // 16 - chdir()
    RELAY_KTHD,         // 17 - chroot()
};