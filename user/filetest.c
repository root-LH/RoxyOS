#include "ulib.h"

#define O_RDWR   0x002
#define O_CREAT  0x100
#define O_TRUNC  0x200
#define O_APPEND 0x400

int lseek(int fd, int offset, int whence);

int main(void)
{
    int fd = open("ftest", O_RDWR | O_CREAT | O_TRUNC);
    print("open fd="); print_int(fd); print("\n");
    print("write="); print_int(write(fd, "hello world", 11)); print("\n");
    print("seek="); print_int(lseek(fd, 6, 0)); print("\n");
    print("write="); print_int(write(fd, "RoxyOS", 6)); print("\n");
    lseek(fd, 0, 0);
    char buf[32];
    int n = read(fd, buf, 5);
    buf[n] = 0;
    print("read5="); print(buf); print("\n");
    n = read(fd, buf, 31);
    buf[n] = 0;
    print("rest="); print(buf); print("\n");
    print("eof="); print_int(read(fd, buf, 31)); print("\n");
    close(fd);
    fd = open("ftest", O_RDWR | O_APPEND);
    write(fd, "!", 1);
    close(fd);
    print("bad fd="); print_int(write(9, "x", 1)); print("\n");
    print("missing="); print_int(open("nope", 0)); print("\n");
    print("type a line: ");
    n = read(0, buf, 31);
    buf[n] = 0;
    print("got["); print(buf); print("]\n");
    return 0;
}
