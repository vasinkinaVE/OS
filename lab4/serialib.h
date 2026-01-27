#ifndef SERIALIB_H
#define SERIALIB_H

#ifdef _WIN32
    #include <windows.h>
    #define SERIAL_PORT HANDLE
#else
    #include <termios.h>
    #include <unistd.h>
    #define SERIAL_PORT int
#endif

#include <string>

class serialib {
public:
    serialib();
    ~serialib();
    int openDevice(const char *Device, const unsigned int Bauds);
    void closeDevice();
    int readChar(char *pByte, size_t timeout_ms = 1000);
    int writeString(const char *str);

private:
    SERIAL_PORT fd;
    #ifndef _WIN32
    termios old_termios;
    #endif
};

#endif