#ifndef RP2350_H
#define RP2350_H

#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "serial/serial.h"

class RP2350
{
public:
    RP2350(const std::string& port, unsigned int baud_rate);
    ~RP2350();

    bool isOpen() const;

    void write(const std::string& data);
    std::string read();

    void click();
    void press();
    void release();
    void move(int x, int y);

    std::atomic<bool> aiming_active;
    std::atomic<bool> shooting_active;
    std::atomic<bool> zooming_active;

private:
    void sendCommand(const std::string& command);
    std::vector<int> splitValue(int value);

    void startListening();
    void listeningThreadFunc();
    void processIncomingLine(const std::string& line);

    serial::Serial serial_;
    std::atomic<bool> is_open_;

    std::thread listening_thread_;
    std::atomic<bool> listening_;
    std::mutex write_mutex_;
};

#endif // RP2350_H
