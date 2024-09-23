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
    // open another socket for the lumen server
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

    // now begin launching the servers -- start with the vfs because everything
    // else will depend on it, and then move to devfs and procfs
    vfs = launchServer("vfs", NULL);
    luxLogf(KPRINT_LEVEL_DEBUG, "connected to virtual file system at socket %d\n", vfs);

    // now start the servers that depend on the vfs
    launchServer("devfs", NULL);    // /dev
    //launchServer("procfs");       // /proc

    // servers that depend on /dev
    launchServer("kbd", NULL);      // generic keyboard interface
    launchServer("lfb", NULL);      // linear frame buffer
    launchServer("pty", NULL);      // psuedo-terminal devices
    launchServer("ps2", NULL);      // PS/2 keyboard and mouse

    // fork lumen into a second process that will be used to continue the boot
    // process, while the initial process will handle kernel requests
    pid_t pid = fork();
    if(!pid) {
        // child process
        mount("", "/dev", "devfs", 0, NULL);
        //mount("", "/proc", "procfs", 0, NULL);

        // test pty
        luxLogf(KPRINT_LEVEL_DEBUG, "attempt to open master terminal multiplexer /dev/ptmx\n");
        int master = open("/dev/ptmx", O_RDWR);
        luxLogf(KPRINT_LEVEL_DEBUG, "opened master with fd %d\n", master);

        // try to find the slave terminal
        char *slaveName = ptsname(master);
        luxLogf(KPRINT_LEVEL_DEBUG, "slave pty is at %s\n", slaveName);
        
        // grant and unlock slave pt
        grantpt(master);
        unlockpt(master);

        // now we can open the slave
        int slave = open(slaveName, O_RDWR);
        luxLogf(KPRINT_LEVEL_DEBUG, "opened slave with fd %d\n", slave);

        luxLogf(KPRINT_LEVEL_DEBUG, "attempting to write 'hello world!' to the slave\n");

        ssize_t s = write(slave, "hello world!", strlen("hello world!"));
        luxLogf(KPRINT_LEVEL_DEBUG, "write() returned %d\n", s);

        luxLogf(KPRINT_LEVEL_DEBUG, "attempting to read from master...\n");

        char buffer[13];
        memset(buffer, 0, 13);

        s = read(master, buffer, 12);
        luxLogf(KPRINT_LEVEL_DEBUG, "read() returned %d\n", s);
        luxLogf(KPRINT_LEVEL_DEBUG, "read buffer: %s\n", buffer);

        for(;;);

        exit(0);
    }

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
    }
}
