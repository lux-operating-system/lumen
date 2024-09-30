/*
 * luxOS - a unix-like operating system 
 * Omar Elghoul, 2024
 *
 * lumen: router enabling communication between the kernel and the servers
 */

#include <liblux/liblux.h>
#include <lumen/lumen.h>
#include <stdlib.h>

/* server(): main server idle function for lumen
 * this really just listens to requests from the kernel and responses from all
 * the other servers
 */

int server() {
    SyscallHeader *req = calloc(1, SERVER_MAX_SIZE);
    SyscallHeader *res = calloc(1, SERVER_MAX_SIZE);
    if(!req || !res) {
        luxLog(KPRINT_LEVEL_DEBUG, "failed to allocate memory for the backlog\n");
        exit(-1);
    }

    kernelsd = luxGetKernelSocket();
    while(1) {
        // receive requests from the kernel and responses from other servers here
        ssize_t s = recv(kernelsd, req, SERVER_MAX_SIZE, MSG_PEEK);     // peek to check size
        if(s > 0 && s <= SERVER_MAX_SIZE) {
            if(req->header.length > SERVER_MAX_SIZE) {
                void *newptr = realloc(req, req->header.length);
                if(!newptr) {
                    luxLogf(KPRINT_LEVEL_ERROR, "failed to allocate memory for message handling\n");
                    exit(-1);
                }

                req = newptr;
            }

            // and read the actual size
            recv(kernelsd, req, req->header.length, 0);

            // request from the kernel
            if((req->header.command < 0x8000) || (req->header.command > MAX_SYSCALL_COMMAND))
                luxLogf(KPRINT_LEVEL_WARNING, "unimplemented syscall request 0x%X len %d from pid %d\n", req->header.command,req->header.length, req->header.requester);
            else
                relaySyscallRequest(req);
        }

        s = recv(vfs, res, SERVER_MAX_SIZE, MSG_PEEK);  // peek to check size
        if(s > 0 && s < SERVER_MAX_SIZE) {
            if(res->header.length > SERVER_MAX_SIZE) {
                void *newptr = realloc(res, res->header.length);
                if(!newptr) {
                    luxLogf(KPRINT_LEVEL_ERROR, "failed to allocate memory for message handling\n");
                    exit(-1);
                }

                res = newptr;
            }

            recv(vfs, res, res->header.length, 0);

            // response from the vfs
            if((res->header.command < 0x8000) || (res->header.command > MAX_SYSCALL_COMMAND))
                luxLogf(KPRINT_LEVEL_WARNING, "unimplemented syscall response 0x%X len %d for pid %d\n", res->header.command,res->header.length, res->header.requester);
            else
                luxSendKernel(res);
        }

        s = recv(kthd, res, SERVER_MAX_SIZE, MSG_PEEK);  // peek to check size
        if(s > 0 && s < SERVER_MAX_SIZE) {
            if(res->header.length > SERVER_MAX_SIZE) {
                void *newptr = realloc(res, res->header.length);
                if(!newptr) {
                    luxLogf(KPRINT_LEVEL_ERROR, "failed to allocate memory for message handling\n");
                    exit(-1);
                }

                res = newptr;
            }

            recv(kthd, res, res->header.length, 0);

            // response from the vfs
            if((res->header.command < 0x8000) || (res->header.command > MAX_SYSCALL_COMMAND))
                luxLogf(KPRINT_LEVEL_WARNING, "unimplemented syscall response 0x%X len %d for pid %d\n", res->header.command,res->header.length, res->header.requester);
            else
                luxSendKernel(res);
        }
    }
}