#ifndef SERIAL_H
#define SERIAL_H

#include "pme.h"

int serial_open(const char *path, int baud);
void serial_close(int fd);
PmeTransport serial_transport(int *fd);
int serial_ports(char paths[][256], int maximum);

#endif
