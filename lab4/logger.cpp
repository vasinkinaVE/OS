#include <iostream>
#include <fstream>
#include <vector>
#include <deque>
#include <string>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <numeric> 
#include "serialib.h"

namespace fs = std::filesystem;
using namespace std::chrono;

std::time_t get_time_t() {
    return std::time(nullptr);
}

std::tm localtime_safe(std::time_t timer) {
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &timer);
#else
    localtime_r(&timer, &tm);
#endif
    return tm;
}

std::string format_time(std::time_t t) {
    auto tm = localtime_safe(t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::vector<std::pair<std::time_t, double>> load_measurements(const std::string& path) {
    std::vector<std::pair<std::time_t, double>> res;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        size_t pos = line.find(',');
        if (pos == std::string::npos) continue;
        std::string ts_str = line.substr(0, pos);
        double temp = std::stod(line.substr(pos + 1));

        std::tm tm = {};
        std::istringstream ss(ts_str);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (ss.fail()) continue;

        std::time_t t = std::mktime(&tm);
        res.push_back({t, temp});
    }
    return res;
}

void save_measurements(const std::string& path, const std::vector<std::pair<std::time_t, double>>& data) {
    std::ofstream f(path);
    for (const auto& m : data) {
        f << format_time(m.first) << "," << m.second << "\n";
    }
}

void cleanup_old(const std::string& path, std::time_t now, long max_age_seconds) {
    if (!fs::exists(path)) return;
    auto data = load_measurements(path);
    data.erase(
        std::remove_if(data.begin(), data.end(),
            [&](const auto& m) { return (now - m.first) > max_age_seconds; }),
        data.end()
    );
    save_measurements(path, data);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: logger <port>\n";
        return 1;
    }

    fs::create_directories("logs");

    serialib ser;
    if (ser.openDevice(argv[1], 9600) != 1) {
        std::cerr << "Cannot open port " << argv[1] << "\n";
        return 1;
    }

    std::deque<std::pair<std::time_t, double>> hourly_buf, daily_buf;

    auto now_t = get_time_t();
    auto tm_now = localtime_safe(now_t);

    int last_hour = tm_now.tm_hour;
    int last_day = tm_now.tm_yday;
    int last_year = tm_now.tm_year + 1900;

    std::string line;
    char c;

    std::cout << "Logger started on " << argv[1] << "\n";

    while (true) {
        if (ser.readChar(&c, 100) > 0) {
            if (c == '\n') {
                if (!line.empty()) {
                    try {
                        double temp = std::stod(line);
                        std::time_t now = get_time_t();
                        auto tm = localtime_safe(now);

                        std::ofstream raw("logs/raw.log", std::ios::app);
                        raw << format_time(now) << "," << temp << "\n";

                        hourly_buf.push_back({now, temp});
                        daily_buf.push_back({now, temp});

                        if (tm.tm_hour != last_hour) {
                            double sum = std::accumulate(hourly_buf.begin(), hourly_buf.end(), 0.0,
                                [](double s, const auto& m) { return s + m.second; });
                            double avg = sum / hourly_buf.size();

                            std::tm hour_tm = tm;
                            hour_tm.tm_min = 0;
                            hour_tm.tm_sec = 0;
                            std::time_t hour_start = std::mktime(&hour_tm);

                            std::ofstream hour("logs/hourly.log", std::ios::app);
                            hour << format_time(hour_start) << "," << avg << "\n";

                            hourly_buf.clear();
                            last_hour = tm.tm_hour;

                            cleanup_old("logs/raw.log", now, 24 * 3600); // 24 ч
                            cleanup_old("logs/hourly.log", now, 30 * 24 * 3600); // 30 дней
                        }

                        if (tm.tm_yday != last_day || (tm.tm_year + 1900) != last_year) {
                            double sum = std::accumulate(daily_buf.begin(), daily_buf.end(), 0.0,
                                [](double s, const auto& m) { return s + m.second; });
                            double avg = sum / daily_buf.size();

                            std::tm day_tm = tm;
                            day_tm.tm_hour = 0;
                            day_tm.tm_min = 0;
                            day_tm.tm_sec = 0;
                            std::time_t day_start = std::mktime(&day_tm);

                            std::ofstream day("logs/daily.log", std::ios::app);
                            day << format_time(day_start) << "," << avg << "\n";

                            daily_buf.clear();
                            last_day = tm.tm_yday;
                            last_year = tm.tm_year + 1900;

                            std::tm year_tm = {};
                            year_tm.tm_year = last_year - 1900;
                            year_tm.tm_mon = 0;
                            year_tm.tm_mday = 1;
                            std::time_t start_of_year = std::mktime(&year_tm);
                            long year_duration = now - start_of_year + 366 * 24 * 3600;
                            cleanup_old("logs/daily.log", now, year_duration);
                        }

                    } catch (...) {}
                    line.clear();
                }
            } else if (c != '\r') {
                line += c;
            }
        }
    }

    ser.closeDevice();
    return 0;
}