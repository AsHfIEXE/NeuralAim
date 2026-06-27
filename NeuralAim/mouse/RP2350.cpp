#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

#include "NeuralAim.h"
#include "RP2350.h"

RP2350::RP2350(const std::string& port, unsigned int baud_rate)
    : is_open_(false),
    listening_(false),
    aiming_active(false),
    shooting_active(false),
    zooming_active(false)
{
    try
    {
        serial_.setPort(port);
        serial_.setBaudrate(baud_rate);
        serial_.open();

        if (serial_.isOpen())
        {
            is_open_ = true;
            std::cout << "[RP2350] Connected! PORT: " << port << std::endl;

            if (config.rp2350_enable_keys)
            {
                startListening();
            }
        }
        else
        {
            std::cerr << "[RP2350] Unable to connect to the port: " << port << std::endl;
        }
    }
    catch (std::exception& e)
    {
        std::cerr << "[RP2350] Error: " << e.what() << std::endl;
    }
}

RP2350::~RP2350()
{
    listening_ = false;

    if (listening_thread_.joinable())
    {
        listening_thread_.join();
    }

    if (serial_.isOpen())
    {
        try { serial_.close(); }
        catch (...) {}
    }

    is_open_ = false;
}

bool RP2350::isOpen() const
{
    return is_open_;
}

void RP2350::write(const std::string& data)
{
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (!is_open_)
        return;

    try
    {
        serial_.write(data);
    }
    catch (...)
    {
        is_open_ = false;
        listening_ = false;
    }
}

std::string RP2350::read()
{
    if (!is_open_)
        return std::string();

    try
    {
        return serial_.readline(65536, "\n");
    }
    catch (...)
    {
        is_open_ = false;
        listening_ = false;
    }

    return std::string();
}

void RP2350::click()
{
    sendCommand("c");
}

void RP2350::press()
{
    sendCommand("p");
}

void RP2350::release()
{
    sendCommand("r");
}

void RP2350::move(int x, int y)
{
    if (!is_open_ || (x == 0 && y == 0))
        return;

    if (config.rp2350_16_bit_mouse)
    {
        write("m" + std::to_string(x) + "," + std::to_string(y) + "\n");
        return;
    }

    std::vector<int> x_parts = splitValue(x);
    std::vector<int> y_parts = splitValue(y);

    size_t max_splits = std::max(x_parts.size(), y_parts.size());
    while (x_parts.size() < max_splits) x_parts.push_back(0);
    while (y_parts.size() < max_splits) y_parts.push_back(0);

    for (size_t i = 0; i < max_splits; ++i)
    {
        write("m" + std::to_string(x_parts[i]) + "," + std::to_string(y_parts[i]) + "\n");
    }
}

void RP2350::sendCommand(const std::string& command)
{
    write(command + "\n");
}

std::vector<int> RP2350::splitValue(int value)
{
    std::vector<int> values;
    int sign = (value < 0) ? -1 : 1;
    int absVal = (value < 0) ? -value : value;

    if (value == 0)
    {
        values.push_back(0);
        return values;
    }

    while (absVal > 127)
    {
        values.push_back(sign * 127);
        absVal -= 127;
    }

    if (absVal != 0)
    {
        values.push_back(sign * absVal);
    }

    return values;
}

void RP2350::startListening()
{
    listening_ = true;
    if (listening_thread_.joinable())
        listening_thread_.join();

    listening_thread_ = std::thread(&RP2350::listeningThreadFunc, this);
}

void RP2350::listeningThreadFunc()
{
    std::string buffer;
    while (listening_ && is_open_)
    {
        try
        {
            size_t available = serial_.available();
            if (available > 0)
            {
                std::string data = serial_.read(available);
                buffer += data;

                size_t pos;
                while ((pos = buffer.find('\n')) != std::string::npos)
                {
                    std::string line = buffer.substr(0, pos);
                    buffer.erase(0, pos + 1);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    processIncomingLine(line);
                }
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        catch (...)
        {
            is_open_ = false;
            break;
        }
    }
}

void RP2350::processIncomingLine(const std::string& line)
{
    try
    {
        bool pressed = false;
        size_t prefixLen = 0;

        if (line.rfind("BD:", 0) == 0)
        {
            pressed = true;
            prefixLen = 3;
        }
        else if (line.rfind("BU:", 0) == 0)
        {
            pressed = false;
            prefixLen = 3;
        }
        else
        {
            return;
        }

        uint16_t buttonId = static_cast<uint16_t>(std::stoi(line.substr(prefixLen)));
        switch (buttonId)
        {
        case 1:
            shooting_active.store(pressed);
            shooting.store(pressed);
            break;
        case 2:
        case 3:
            zooming_active.store(pressed);
            zooming.store(pressed);
            break;
        case 5:
            aiming_active.store(pressed);
            aiming.store(pressed);
            break;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[RP2350] Error processing line '" << line << "': " << e.what() << std::endl;
    }
}
