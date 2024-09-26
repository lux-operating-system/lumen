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
#include <sys/stat.h>
#include <sys/lux/lux.h>        // execrdv
#include <liblux/liblux.h>
#include <lumen/lumen.h>
#include <fnctl.h>
#include <dirent.h>

/* socket descriptors for the kernel connection and for the lumen server */
int kernelsd, lumensd;
pid_t self;

int vfs = -1;

/* launchServer(): launches a server from the ramdisk
 * params: name - file name of the server executable
 * params: addr - destination to store the address of the server, NULL if not needed
 * returns: socket connection of the server, -1 on fail
 */

int launchServer(const char *name, struct sockaddr_un *addr) {
    pid_t pid = fork();
    if(!pid) {
        // child process
        execrdv(name, NULL);
        luxLogf(KPRINT_LEVEL_ERROR, "unable to start server '%s'\n", name);
        exit(-1);   // unreachable on success
    }

    // parent
    if(pid < 0) return -1;  // fork failed

    // now wait for the server to actually start
    int sd = -1;
    struct sockaddr_un peer;
    socklen_t peerlen = sizeof(struct sockaddr_un);

    while(sd < 0) {
        sd = accept(lumensd, (struct sockaddr *) &peer, &peerlen);
        if(sd >= 0 && !strcmp((const char *) &peer.sun_path[7], name)) break;
        else sched_yield();
    }

    // wait for the ready message
    MessageHeader msg;
    for(;;) {
        ssize_t s = recv(sd, &msg, sizeof(MessageHeader), 0);
        if((s > 0) && (s <= sizeof(MessageHeader)) && (msg.command == COMMAND_LUMEN_READY)) {
            luxLogf(KPRINT_LEVEL_DEBUG, "completed startup of server '%s' with pid %d\n", name, pid);
            break;
        }
    }

    if(addr) memcpy(addr, &peer, peerlen);
    return sd;
}

/* main(): lumen's entry point
 * this function completes the boot process, takes no parameters, and should
 * never return - the kernel panics if lumen terminates
 */

int main(int argc, char **argv) {
    // this is the first process that runs when the system boots
    // start by opening a socket for the lumen server
    struct sockaddr_un lumen;
    lumen.sun_family = AF_UNIX;
    strcpy(lumen.sun_path, SERVER_LUMEN_PATH);

    lumensd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
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

    // begin launching the servers -- start with the vfs because everything
    // else will depend on it
    vfs = launchServer("vfs", NULL);
    luxLogf(KPRINT_LEVEL_DEBUG, "connected to virtual file system at socket %d\n", vfs);

    // fork into a second process that will be used for the lumen server
    // this has to be done AFTER the vfs is loaded because the server will need
    // access to the vfs socket descriptor to relay other syscalls

    // TODO: replace this fork() with a POSIX thread after i implement them
    pid_t pid = fork();
    if(!pid) return server();

    // now start the servers that depend on the vfs
    launchServer("devfs", NULL);    // /dev
    launchServer("procfs", NULL);   // /proc

    // mount devfs and procfs
    mount("", "/dev", "devfs", 0, NULL);
    mount("", "/proc", "procfs", 0, NULL);

    // device drivers
    launchServer("kbd", NULL);      // generic keyboard interface
    launchServer("ps2", NULL);      // PS/2 keyboard and mouse
    launchServer("lfb", NULL);      // linear frame buffer
    launchServer("pty", NULL);      // psuedo-terminal devices
    launchServer("pci", NULL);      // PCI bus
    launchServer("nvme", NULL);     // NVMe SSDs

    for(;;) sched_yield();          // kernel will panic if lumen exits!
}
