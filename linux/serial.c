#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "serial.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static speed_t serial_speed(int baud)
{
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default: return 0;
    }
}

int serial_open(const char *path, int baud)
{
    speed_t speed = serial_speed(baud);
    if (!speed) { errno = EINVAL; return -1; }
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    struct termios settings;
    if (tcgetattr(fd, &settings) != 0) { close(fd); return -1; }
    cfmakeraw(&settings);
    settings.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
    settings.c_cflag |= CS8 | CLOCAL | CREAD;
#ifdef CRTSCTS
    settings.c_cflag &= ~CRTSCTS;
#endif
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;
    if (cfsetispeed(&settings, speed) != 0 || cfsetospeed(&settings, speed) != 0 ||
        tcsetattr(fd, TCSANOW, &settings) != 0 || tcflush(fd, TCIOFLUSH) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void serial_close(int fd)
{
    if (fd >= 0) close(fd);
}

static int serial_write(void *context, const uint8_t *data, size_t size, int timeout_ms)
{
    int fd = *(int *)context;
    struct pollfd item = {.fd = fd, .events = POLLOUT};
    int result;
    do { result = poll(&item, 1, timeout_ms); } while (result < 0 && errno == EINTR);
    if (result <= 0 || (item.revents & (POLLERR | POLLHUP | POLLNVAL))) return -1;
    ssize_t sent = write(fd, data, size);
    return sent > 0 ? (int)sent : -1;
}

static int serial_read(void *context, uint8_t *data, size_t size, int timeout_ms)
{
    int fd = *(int *)context;
    struct pollfd item = {.fd = fd, .events = POLLIN};
    int result;
    do { result = poll(&item, 1, timeout_ms); } while (result < 0 && errno == EINTR);
    if (result == 0) return 0;
    if (result < 0 || (item.revents & (POLLERR | POLLHUP | POLLNVAL))) return -1;
    ssize_t received = read(fd, data, size);
    return received > 0 ? (int)received : -1;
}

PmeTransport serial_transport(int *fd)
{
    PmeTransport port = {.context = fd, .write = serial_write, .read = serial_read};
    return port;
}

int serial_ports(char paths[][256], int maximum)
{
    if (maximum <= 0) return 0;
    const char *patterns[] = {"/dev/serial/by-id/*", "/dev/ttyUSB*", "/dev/ttyACM*"};
    int count = 0;
    for (size_t p = 0; p < sizeof(patterns) / sizeof(patterns[0]); p++) {
        glob_t list = {0};
        if (glob(patterns[p], 0, NULL, &list) == 0) {
            for (size_t i = 0; i < list.gl_pathc && count < maximum; i++) {
                if (strlen(list.gl_pathv[i]) >= 256) continue;
                int exists = 0;
                for (int j = 0; j < count; j++)
                    if (strcmp(paths[j], list.gl_pathv[i]) == 0) exists = 1;
                if (!exists) snprintf(paths[count++], 256, "%s", list.gl_pathv[i]);
            }
        }
        globfree(&list);
    }
    return count;
}
