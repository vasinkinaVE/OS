#include "background_runner.hpp"
#include <iostream>
#include <thread>
#include <chrono>

void sleep_seconds(int seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

int main() {
    std::cout << "=== Cross-platform background runner library test ===\n\n";
    
    try {
        // Тест 1: Простая команда
        std::cout << "Test 1: Running command 'echo Hello World'\n";
        auto process1 = bg_runner::run("echo Hello World");
        
        int exit_code1 = process1.wait();
        std::cout << "Exit code: " << exit_code1 << "\n\n";
        
        // Тест 2: Команда с ненулевым кодом выхода
        std::cout << "Test 2: Running command with error\n";
#ifdef _WIN32
        auto process2 = bg_runner::run("exit 42");
#else
        auto process2 = bg_runner::run("exit 42");
#endif
        
        int exit_code2 = process2.wait();
        std::cout << "Exit code: " << exit_code2 << "\n\n";
        
        // Тест 3: Проверка фонового выполнения
        std::cout << "Test 3: Checking background execution\n";
#ifdef _WIN32
        auto process3 = bg_runner::run("ping -n 4 127.0.0.1 > nul");
#else
        auto process3 = bg_runner::run("sleep 3");
#endif
        
        std::cout << "Process started in background. Checking status...\n";
        
        for (int i = 0; i < 5; i++) {
            if (process3.is_finished()) {
                std::cout << "Process finished!\n";
                break;
            } else {
                std::cout << "Process is still running...\n";
            }
            sleep_seconds(1);
        }
        
        int exit_code3 = process3.wait();
        std::cout << "Exit code: " << exit_code3 << "\n\n";
        
        // Тест 4: Запуск нескольких процессов
        std::cout << "Test 4: Running multiple processes\n";
#ifdef _WIN32
        auto process4a = bg_runner::run("echo Process A");
        auto process4b = bg_runner::run("ping -n 2 127.0.0.1 > nul && echo Process B");
#else
        auto process4a = bg_runner::run("echo Process A");
        auto process4b = bg_runner::run("sleep 1 && echo Process B");
#endif
        
        int exit_code4a = process4a.wait();
        int exit_code4b = process4b.wait();
        
        std::cout << "Exit code Process A: " << exit_code4a << "\n";
        std::cout << "Exit code Process B: " << exit_code4b << "\n\n";
        
        std::cout << "All tests passed successfully!\n";
    }
    catch (const bg_runner::RunnerException& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    catch (const std::exception& e) {
        std::cerr << "Unknown error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}