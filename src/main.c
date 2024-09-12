/*
 * luxOS - a unix-like operating system 
 * Omar Elghoul, 2024
 *
 * lumen: router enabling communication between the kernel and the servers
 */

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mount.h>
#include <sys/lux/lux.h>        // execrdv
#include <liblux/liblux.h>
#include <lumen/lumen.h>

/* socket descriptors for the kernel connection and for the lumen server */
int kernelsd, lumensd;
pid_t self;

int vfs = -1;

/* launchServer(): launches a server from the ramdisk
 * params: name: file name of the server executable
 * returns: pid of the server
 */

pid_t launchServer(const char *name) {
    pid_t pid = fork();
    if(!pid) {
        // child process
        execrdv(name, NULL);
        exit(-1);
    }

    // parent
    return pid;
}

int main(int argc, char **argv) {
    // this is the first process that runs when the system boots
    // start by opening a socket for the lumen server
    // open another socket for the lumen server
    struct sockaddr_un lumen;
    lumen.sun_family = AF_UNIX;
    strcpy(lumen.sun_path, SERVER_LUMEN_PATH);

    lumensd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if(lumensd < 0) return -1;

    // bind local address
    int status = bind(lumensd, (const struct sockaddr *)&lumen, sizeof(struct sockaddr_un));
    if(status) return -1;

    // set up lumen as a listener
    status = listen(lumensd, 0);    // use default backlog
    if(status) return -1;

    // then establish connection with the kernel
    luxInitLumen();

    luxLogf(KPRINT_LEVEL_DEBUG, "lumen is listening on socket %d: %s\n", lumensd, lumen.sun_path);
    luxLog(KPRINT_LEVEL_DEBUG, "starting launch of lumen core servers...\n");

    // now begin launching the servers -- these ones will be hard coded because
    // they are necessary for everything and must be located on the ramdisk as
    // we still don't have any file system or storage drivers at this stage
    launchServer("vfs");        // virtual file system
    //launchServer("devfs");      // /dev
    //launchServer("fb");         // framebuffer output
    //launchServer("tty");        // terminal I/O
    //launchServer("procfs");     // /proc

    kernelsd = luxGetKernelSocket();

    struct sockaddr_un peer;
    socklen_t peerlen;
    int socket = -1;

    while(vfs <= 0) {
        // now we need to essentially loop over the lumen socket and wait for
        // all the servers to be launched
        memset(&peer, 0, sizeof(struct sockaddr));
        peerlen = sizeof(struct sockaddr_un);
        socket = accept(lumensd, (struct sockaddr *) &peer, &peerlen);
        if(socket > 0) {
            if(!strcmp(peer.sun_path, "lux:///vfs")) vfs = socket;
        }

        sched_yield();
    }

    luxLogf(KPRINT_LEVEL_DEBUG, "connected to virtual file system at socket %d\n", vfs);

    // allow some time for the other servers to start up
    for(int i = 0; i < 30; i++) sched_yield();

    // fork lumen into a second process that will be used to continue the boot
    // process, while the initial process will handle kernel requests
    pid_t pid = fork();
    if(!pid) {
        // child process
        luxLog(KPRINT_LEVEL_DEBUG, "mounting /dev ...\n");
        mount("", "/dev", "devfs", 0, NULL);

        while(1) sched_yield();
    }

    SyscallHeader *req = calloc(1, SERVER_MAX_SIZE);
    SyscallHeader *res = calloc(1, SERVER_MAX_SIZE);
    if(!req || !res) {
        luxLog(KPRINT_LEVEL_DEBUG, "failed to allocate memory for the backlog\n");
        exit(-1);
    }

    ssize_t s;

    while(1) {
        // receive requests from the kernel and responses from other servers here
        s = recv(luxGetKernelSocket(), req, SERVER_MAX_SIZE, 0);
        if(s > 0) {
            // request from the kernel
            if((req->header.command < 0x8000) || (req->header.command > MAX_SYSCALL_COMMAND))
                luxLogf(KPRINT_LEVEL_WARNING, "unimplemented syscall request 0x%X len %d from pid %d\n", req->header.command,req->header.length, req->header.requester);
            else
                relaySyscallRequest(req);

            memset(req, 0, req->header.length);
        }

        s = recv(vfs, res, SERVER_MAX_SIZE, 0);
        if(s > 0) {
            // response from the vfs
            luxLogf(KPRINT_LEVEL_WARNING, "unimplemented syscall response 0x%X len %d for pid %d\n", req->header.command,req->header.length, req->header.requester);
        }

        if(s <= 0) sched_yield();
    }
}
