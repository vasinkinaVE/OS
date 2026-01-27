#include "serialib.h"
#include <cstring>
#include <iostream>

#ifndef _WIN32
    #include <fcntl.h>   
    #include <sys/select.h> 
    #include <cerrno>      
#endif

#ifdef _WIN32
serialib::serialib() : fd(INVALID_HANDLE_VALUE) {}
serialib::~serialib() { if (fd != INVALID_HANDLE_VALUE) CloseHandle(fd); }

int serialib::openDevice(const char *Device, const unsigned int Bauds) {
    fd = CreateFileA(Device, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL, NULL);
    if (fd == INVALID_HANDLE_VALUE) return -1;

    DCB dcb;
    SecureZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    dcb.BaudRate = Bauds;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    SetCommState(fd, &dcb);

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(fd, &timeouts);
    return 1;
}

void serialib::closeDevice() {
    if (fd != INVALID_HANDLE_VALUE) CloseHandle(fd);
    fd = INVALID_HANDLE_VALUE;
}

int serialib::readChar(char *pByte, size_t timeout_ms) {
    DWORD dwBytesRead = 0;
    if (!ReadFile(fd, pByte, 1, &dwBytesRead, NULL)) return -1;
    return dwBytesRead;
}

int serialib::writeString(const char *str) {
    DWORD dwBytesWritten = 0;
    WriteFile(fd, str, strlen(str), &dwBytesWritten, NULL);
    return dwBytesWritten;
}

#else 

serialib::serialib() : fd(-1) {}
serialib::~serialib() { 
    if (fd >= 0) { 
        tcsetattr(fd, TCSANOW, &old_termios); 
        close(fd); 
    } 
}

int serialib::openDevice(const char *Device, const unsigned int Bauds) {
    fd = open(Device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) return -1;

    fcntl(fd, F_SETFL, 0);
    struct termios options;
    tcgetattr(fd, &options);
    old_termios = options;

    speed_t speed;
    switch (Bauds) {
        case 9600:   speed = B9600; break;
        case 19200:  speed = B19200; break;
        case 38400:  speed = B38400; break;
        case 57600:  speed = B57600; break;
        case 115200: speed = B115200; break;
        default:     speed = B9600;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;

    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;

    tcsetattr(fd, TCSANOW, &options);
    return 1;
}

void serialib::closeDevice() {
    if (fd >= 0) {
        tcsetattr(fd, TCSANOW, &old_termios);
        close(fd);
        fd = -1;
    }
}

int serialib::readChar(char *pByte, size_t timeout_ms) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int result = select(fd + 1, &read_fds, NULL, NULL, &tv);
    if (result <= 0) return -1;
    ssize_t n = read(fd, pByte, 1);
    return (n > 0) ? n : -1;
}

int serialib::writeString(const char *str) {
    return write(fd, str, strlen(str));
}
#endif