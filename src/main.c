/*
 * luxOS - a unix-like operating system 
 * Omar Elghoul, 2024
 *
 * lumen: router enabling communication between the kernel and the servers
 */

#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/lux/lux.h>        // execrdv
#include <liblux/liblux.h>

/* socket descriptors for the kernel connection and for the lumen server */
int kernelsd, lumensd;
pid_t self;

static int vfs = -1;

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
    // start by establishing a socket connection with the kernel
    luxInitLumen();

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

    luxLog(KPRINT_LEVEL_DEBUG, "starting launch of lumen core servers...\n");

    // now begin launching the servers -- these ones will be hard coded because
    // they are necessary for everything and must be located on the ramdisk as
    // we still don't have any file system or storage drivers at this stage
    launchServer("vfs");        // virtual file system
    launchServer("devfs");      // /dev
    launchServer("fb");         // framebuffer output
    launchServer("tty");        // terminal I/O
    launchServer("procfs");     // /proc

    // allow some time for server start up
    for(int i = 0; i < 10; i++) sched_yield();

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

    luxLog(KPRINT_LEVEL_DEBUG, "virtual file system server launched\n");
    luxLog(KPRINT_LEVEL_WARNING, "TODO: mount devfs and procfs\n");

    // TODO: mount devfs and procfs
    while(1);
}
