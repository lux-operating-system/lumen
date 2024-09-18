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
 * params: name: file name of the server executable
 * returns: pid of the server
 */

pid_t launchServer(const char *name) {
    pid_t pid = fork();
    if(!pid) {
        // child process
        luxLogf(KPRINT_LEVEL_DEBUG, "starting server '%s'\n", name);
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

    // now begin launching the servers -- start with the vfs because everything
    // else will depend on it, and then move to devfs and procfs
    launchServer("vfs");

    kernelsd = luxGetKernelSocket();

    struct sockaddr_un peer;
    socklen_t peerlen;

    while(vfs <= 0) {
        // now we need to essentially loop over the lumen socket and wait for
        // all the servers to be launched
        memset(&peer, 0, sizeof(struct sockaddr));
        peerlen = sizeof(struct sockaddr_un);
        int socket = accept(lumensd, (struct sockaddr *) &peer, &peerlen);
        if(socket > 0) {
            if(!strcmp(peer.sun_path, "lux:///vfs")) vfs = socket;
        }

        sched_yield();
    }

    luxLogf(KPRINT_LEVEL_DEBUG, "connected to virtual file system at socket %d\n", vfs);

    // now start the servers that depend on the vfs
    launchServer("devfs");          // /dev
    //launchServer("procfs");       // /proc
    //launchServer("lfb");          // linear frame buffer
    //launchServer("tty");          // terminal emulator

    // allow some time for the other servers to start up
    for(int i = 0; i < 100; i++) sched_yield();

    // fork lumen into a second process that will be used to continue the boot
    // process, while the initial process will handle kernel requests
    pid_t pid = fork();
    if(!pid) {
        // child process
        mount("", "/dev", "devfs", 0, NULL);
        //mount("", "/proc", "procfs", 0, NULL);

        struct stat buffer;
        if(!stat("/dev/null", &buffer)) {       // test
            luxLogf(KPRINT_LEVEL_DEBUG, "test stat() for /dev/null: owner 0x%X mode: 0x%X (%s%s%s%s%s%s%s%s%s)\n", buffer.st_uid, buffer.st_mode,
                buffer.st_mode & S_IRUSR ? "r" : "-", buffer.st_mode & S_IWUSR ? "w" : "-", buffer.st_mode & S_IXUSR ? "x" : "-",
                buffer.st_mode & S_IRGRP ? "r" : "-", buffer.st_mode & S_IWGRP ? "w" : "-", buffer.st_mode & S_IXGRP ? "x" : "-",
                buffer.st_mode & S_IROTH ? "r" : "-", buffer.st_mode & S_IWOTH ? "w" : "-", buffer.st_mode & S_IXOTH ? "x" : "-");
        }

        luxLogf(KPRINT_LEVEL_DEBUG, "attempt to open /dev/random\n");

        // test
        int fd = open("/dev/random", O_RDONLY);
        luxLogf(KPRINT_LEVEL_DEBUG, "opened file descriptor %d\n", fd);

        unsigned char buf[4];
        ssize_t size = read(fd, &buf, 4);

        luxLogf(KPRINT_LEVEL_DEBUG, "read() returned: %d bytes read\n", size);
        luxLogf(KPRINT_LEVEL_DEBUG, "read() test buffer: %02X %02X %02X %02X\n", buf[0], buf[1], buf[2], buf[3]);

        while(1);
    }

    SyscallHeader *req = calloc(1, SERVER_MAX_SIZE);
    SyscallHeader *res = calloc(1, SERVER_MAX_SIZE);
    if(!req || !res) {
        luxLog(KPRINT_LEVEL_DEBUG, "failed to allocate memory for the backlog\n");
        exit(-1);
    }

    while(1) {
        // receive requests from the kernel and responses from other servers here
        ssize_t s = recv(luxGetKernelSocket(), req, SERVER_MAX_SIZE, 0);
        if(s > 0 && s < SERVER_MAX_SIZE) {
            // request from the kernel
            if((req->header.command < 0x8000) || (req->header.command > MAX_SYSCALL_COMMAND))
                luxLogf(KPRINT_LEVEL_WARNING, "unimplemented syscall request 0x%X len %d from pid %d\n", req->header.command,req->header.length, req->header.requester);
            else
                relaySyscallRequest(req);
        }

        s = recv(vfs, res, SERVER_MAX_SIZE, 0);
        if(s > 0 && s < SERVER_MAX_SIZE) {
            // response from the vfs
            if((res->header.command < 0x8000) || (res->header.command > MAX_SYSCALL_COMMAND))
                luxLogf(KPRINT_LEVEL_WARNING, "unimplemented syscall response 0x%X len %d for pid %d\n", res->header.command,res->header.length, res->header.requester);
            else
                luxSendKernel(res);
        }
    }
}
