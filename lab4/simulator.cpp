#include <iostream>
#include <random>
#include <thread>
#include <chrono>
#include <string>
#include "serialib.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: simulator <port>\n";
        return 1;
    }

    std::string port = argv[1];
    serialib sim;
    if (sim.openDevice(port.c_str(), 9600) != 1) {
        std::cerr << "Failed to open " << port << "\n";
        return 1;
    }

    std::cout << "Simulator started on " << port << "\n";
    std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<> dis(0.0, 28.0);

    while (true) {
        double temp = dis(gen);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f\n", temp);
        sim.writeString(buf);
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    sim.closeDevice();
    return 0;
}