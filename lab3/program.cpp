#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/mman.h>
    #include <fcntl.h>
    #include <sys/wait.h>
    #include <sys/file.h> // for flock
#endif

struct SharedData {
    volatile long long counter;
    volatile bool child1_done;
    volatile bool child2_done;
};

const char* LOG_FILE = "program.log";
const char* SHM_NAME = "/my_shared_counter";
#ifdef _WIN32
    const char* WIN_SHM_NAME = "MySharedCounter";
#endif

std::atomic<bool> should_exit(false);
std::atomic<bool> is_master(false);
std::mutex log_mutex;

SharedData* g_shared = nullptr;

#ifdef _WIN32
HANDLE g_shm_handle = nullptr;
HANDLE g_lock_file = INVALID_HANDLE_VALUE;
#else
int g_shm_fd = -1;
int g_lock_fd = -1;
#endif

std::string get_current_time() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    localtime_r(&time_t, &tm);
#endif
    
    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    char full_buffer[120];
    std::snprintf(full_buffer, sizeof(full_buffer), "%s.%03d", buffer, (int)ms.count());
    return std::string(full_buffer);
}

pid_t get_process_id() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}

void log_message(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::ofstream log_file(LOG_FILE, std::ios::app);
    if (log_file.is_open()) {
        log_file << "[" << get_current_time() << "] [PID: " << get_process_id() << "] " << message << std::endl;
        log_file.flush(); // Portable and sufficient
    }
}

void atomic_multiply_by_2() {
#ifdef _WIN32
    LONGLONG expected, desired;
    do {
        expected = g_shared->counter;
        desired = expected * 2;
    } while (InterlockedCompareExchange64(&g_shared->counter, desired, expected) != expected);
#else
    long long expected, desired;
    do {
        expected = g_shared->counter;
        desired = expected * 2;
    } while (!__sync_bool_compare_and_swap(&g_shared->counter, expected, desired));
#endif
}

void atomic_divide_by_2() {
#ifdef _WIN32
    LONGLONG expected, desired;
    do {
        expected = g_shared->counter;
        desired = expected / 2;
    } while (InterlockedCompareExchange64(&g_shared->counter, desired, expected) != expected);
#else
    long long expected, desired;
    do {
        expected = g_shared->counter;
        desired = expected / 2;
    } while (!__sync_bool_compare_and_swap(&g_shared->counter, expected, desired));
#endif
}

bool setup_shared_memory(bool create) {
#ifdef _WIN32
    if (create) {
        g_shm_handle = CreateFileMappingA(
            INVALID_HANDLE_VALUE,
            NULL,
            PAGE_READWRITE,
            0,
            sizeof(SharedData),
            WIN_SHM_NAME
        );
        if (!g_shm_handle) return false;
    } else {
        g_shm_handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, WIN_SHM_NAME);
        if (!g_shm_handle) return false;
    }
    g_shared = (SharedData*)MapViewOfFile(g_shm_handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    if (!g_shared) return false;
    if (create) {
        g_shared->counter = 0;
        g_shared->child1_done = true;
        g_shared->child2_done = true;
    }
#else
    if (create) {
        g_shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
        if (g_shm_fd == -1) return false;
        if (ftruncate(g_shm_fd, sizeof(SharedData)) == -1) {
            close(g_shm_fd);
            return false;
        }
    } else {
        g_shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (g_shm_fd == -1) return false;
    }
    g_shared = (SharedData*)mmap(nullptr, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, g_shm_fd, 0);
    if (g_shared == MAP_FAILED) {
        g_shared = nullptr;
        return false;
    }
    if (create) {
        g_shared->counter = 0;
        g_shared->child1_done = true;
        g_shared->child2_done = true;
    }
#endif
    return true;
}

void cleanup_shared_memory() {
    if (g_shared) {
#ifdef _WIN32
        UnmapViewOfFile(g_shared);
        if (g_shm_handle) CloseHandle(g_shm_handle);
#else
        munmap(g_shared, sizeof(SharedData));
        if (is_master) {
            shm_unlink(SHM_NAME);
        }
        if (g_shm_fd != -1) close(g_shm_fd);
#endif
        g_shared = nullptr;
    }
}

void release_master_lock() {
#ifdef _WIN32
    if (g_lock_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_lock_file);
        g_lock_file = INVALID_HANDLE_VALUE;
    }
#else
    if (g_lock_fd != -1) {
        close(g_lock_fd);
        g_lock_fd = -1;
    }
#endif
}

void signal_handler(int signal) {
    should_exit = true;
}

void counter_thread() {
    while (!should_exit) {
#ifdef _WIN32
        InterlockedIncrement64(&g_shared->counter);
#else
        __sync_fetch_and_add(&g_shared->counter, 1);
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

void log_thread() {
    while (!should_exit) {
        if (is_master) {
            log_message("Counter value: " + std::to_string(g_shared->counter));
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void spawn_thread() {
    while (!should_exit) {
        if (is_master) {
            if (!g_shared->child1_done || !g_shared->child2_done) {
                log_message("Warning: Previous child processes are still running, skipping spawn");
            } else {
                g_shared->child1_done = false;
                g_shared->child2_done = false;

#ifdef _WIN32
                std::string cmd1 = "program.exe 1";
                std::string cmd2 = "program.exe 2";
                STARTUPINFO si1{}, si2{};
                PROCESS_INFORMATION pi1{}, pi2{};
                si1.cb = sizeof(si1);
                si2.cb = sizeof(si2);
                CreateProcessA(NULL, (LPSTR)cmd1.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si1, &pi1);
                CreateProcessA(NULL, (LPSTR)cmd2.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si2, &pi2);
                CloseHandle(pi1.hProcess); CloseHandle(pi1.hThread);
                CloseHandle(pi2.hProcess); CloseHandle(pi2.hThread);
#else
                pid_t pid1 = fork();
                if (pid1 == 0) {
                    execl("./program", "./program", "1", (char*)nullptr);
                    perror("execl failed for child 1");
                    _exit(1);
                }
                pid_t pid2 = fork();
                if (pid2 == 0) {
                    execl("./program", "./program", "2", (char*)nullptr);
                    perror("execl failed for child 2");
                    _exit(1);
                }
#endif
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

void child_process_1() {
    log_message("Child process 1 started");
#ifdef _WIN32
    InterlockedExchangeAdd64(&g_shared->counter, 10);
#else
    __sync_fetch_and_add(&g_shared->counter, 10);
#endif
    log_message("Child process 1: counter increased by 10 (new value: " + std::to_string(g_shared->counter) + ")");
    log_message("Child process 1 exiting");
    g_shared->child1_done = true;
    exit(0);
}

void child_process_2() {
    log_message("Child process 2 started");
    
    atomic_multiply_by_2();
    log_message("Child process 2: counter doubled (new value: " + std::to_string(g_shared->counter) + ")");
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    atomic_divide_by_2();
    log_message("Child process 2: counter halved (new value: " + std::to_string(g_shared->counter) + ")");
    
    log_message("Child process 2 exiting");
    g_shared->child2_done = true;
    exit(0);
}

bool become_master() {
#ifdef _WIN32
    g_lock_file = CreateFileA(
        LOG_FILE,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (g_lock_file == INVALID_HANDLE_VALUE) {
        return false;
    }

    OVERLAPPED ov = {0};
    if (!LockFileEx(g_lock_file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
        CloseHandle(g_lock_file);
        g_lock_file = INVALID_HANDLE_VALUE;
        return false;
    }

    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d\n", (int)get_process_id());
    DWORD bytes_written;
    SetFilePointer(g_lock_file, 0, NULL, FILE_BEGIN);
    WriteFile(g_lock_file, pid_str, (DWORD)strlen(pid_str), &bytes_written, NULL);
    FlushFileBuffers(g_lock_file);

    return true;

#else
    g_lock_fd = open(LOG_FILE, O_CREAT | O_RDWR, 0666);
    if (g_lock_fd == -1) {
        return false;
    }

    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) == -1) {
        close(g_lock_fd);
        g_lock_fd = -1;
        return false;
    }

    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d\n", (int)get_process_id());
    lseek(g_lock_fd, 0, SEEK_SET);
    write(g_lock_fd, pid_str, strlen(pid_str));
    fsync(g_lock_fd);

    return true;
#endif
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        if (!setup_shared_memory(false)) {
            std::cerr << "Failed to attach to shared memory" << std::endl;
            return 1;
        }
        int child_type = std::atoi(argv[1]);
        if (child_type == 1) {
            child_process_1();
        } else if (child_type == 2) {
            child_process_2();
        }
        return 0;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifndef _WIN32
    std::signal(SIGHUP, signal_handler);
#endif

    is_master = become_master();

    if (!setup_shared_memory(is_master)) {
        std::cerr << "Failed to setup shared memory" << std::endl;
        if (is_master) release_master_lock();
        return 1;
    }

    log_message("Process started");

    std::thread counter_t(counter_thread);
    std::thread log_t, spawn_t;
    if (is_master) {
        log_t = std::thread(log_thread);
        spawn_t = std::thread(spawn_thread);
    }

    std::string input;
    while (!should_exit) {
        std::cout << "Current counter: " << g_shared->counter << std::endl;
        std::cout << "Enter new counter value (or 'q' to quit): ";
        if (std::getline(std::cin, input)) {
            if (input == "q" || input == "Q") {
                should_exit = true;
                break;
            }
            try {
                long long val = std::stoll(input);
#ifdef _WIN32
                InterlockedExchange64(&g_shared->counter, val);
#else
                long long expected;
                do {
                    expected = g_shared->counter;
                } while (!__sync_bool_compare_and_swap(&g_shared->counter, expected, val));
#endif
                std::cout << "Counter updated to " << val << std::endl;
            } catch (...) {
                std::cout << "Invalid input. Please enter a number." << std::endl;
            }
        }
    }

    if (counter_t.joinable()) counter_t.join();
    if (is_master) {
        if (log_t.joinable()) log_t.join();
        if (spawn_t.joinable()) spawn_t.join();
        release_master_lock();
    }

    log_message("Process exiting");
    cleanup_shared_memory();
    return 0;
}