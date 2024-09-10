/*
 * luxOS - a unix-like operating system 
 * Omar Elghoul, 2024
 *
 * lumen: router enabling communication between the kernel and the servers
 */

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/lux/lux.h>        // execrdv

/* socket descriptors for the kernel connection and for the lumen server */
int kernelsd, lumensd;
pid_t self;

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
    self = getpid();

    struct sockaddr_un kernel;
    kernel.sun_family = AF_UNIX;
    strcpy(kernel.sun_path, "lux:///kernel");   // special path, not a true file

    struct sockaddr_un lumen;
    lumen.sun_family = AF_UNIX;
    strcpy(lumen.sun_path, "lux:///lumen");

    kernelsd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);  // lumen doesn't block
    if(kernelsd < 0) return -1;     // we don't have any output device yet at this stage

    // and connect to the kernel
    int status = connect(kernelsd, (const struct sockaddr *)&kernel, sizeof(struct sockaddr_un));
    if(status) return -1;

    // open another socket for the lumen server
    lumensd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if(lumensd < 0) return -1;

    // bind local address
    status = bind(lumensd, (const struct sockaddr *)&lumen, sizeof(struct sockaddr_un));
    if(status) return -1;

    // set up lumen as a listener
    status = listen(lumensd, 0);    // use default backlog
    if(status) return -1;

    // now begin launching the servers -- these ones will be hard coded because
    // they are necessary for everything and must be located on the ramdisk as
    // we still don't have any file system or storage drivers at this stage
    launchServer("vfs");        // virtual file system
    launchServer("devfs");      // /dev
    launchServer("fb");         // framebuffer output
    launchServer("tty");        // terminal I/O
    launchServer("procfs");     // /proc

    while(1);
}
