/*
 * luxOS - a unix-like operating system 
 * Omar Elghoul, 2024
 *
 * lumen: router enabling communication between the kernel and the servers
 */

#include <sys/types.h>
#include <liblux/liblux.h>

#define RELAY_VFS                   1

extern int kernelsd, lumensd;
extern pid_t self;
extern int vfs;

void relaySyscallRequest(SyscallHeader *);
void relaySyscallResponse(SyscallHeader *);
