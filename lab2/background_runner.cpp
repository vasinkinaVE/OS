#include "background_runner.hpp"
#include <string>
#include <stdexcept>

#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
#else
    #include <sys/wait.h>
    #include <unistd.h>
    #include <signal.h>
    #include <errno.h>
#endif

namespace bg_runner {

int BackgroundProcess::wait() {
    if (!valid()) {
        throw RunnerException("Invalid process");
    }
    
#ifdef _WIN32
    HANDLE hProcess = static_cast<HANDLE>(process_handle_);
    
    if (WaitForSingleObject(hProcess, INFINITE) == WAIT_FAILED) {
        CloseHandle(hProcess);
        throw RunnerException("Failed to wait for process");
    }
    
    DWORD exitCode;
    if (!GetExitCodeProcess(hProcess, &exitCode)) {
        CloseHandle(hProcess);
        throw RunnerException("Failed to get exit code");
    }
    
    CloseHandle(hProcess);
    process_handle_ = nullptr;
    return static_cast<int>(exitCode);
#else
    if (finished_) {
        int code = exit_code_;
        exit_code_ = 0;
        finished_ = false;
        process_id_ = -1;
        return code;
    }
    
    pid_t child_pid = static_cast<pid_t>(process_id_);
    int status;
    
    if (waitpid(child_pid, &status, 0) == -1) {
        throw RunnerException("Failed to wait for process");
    }
    
    process_id_ = -1;
    
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    
    return -1;
#endif
}

bool BackgroundProcess::is_finished() const {
    if (!valid()) {
        throw RunnerException("Invalid process");
    }
    
#ifdef _WIN32
    HANDLE hProcess = static_cast<HANDLE>(process_handle_);
    DWORD exitCode;
    
    if (!GetExitCodeProcess(hProcess, &exitCode)) {
        throw RunnerException("Failed to get exit code");
    }
    
    return (exitCode != STILL_ACTIVE);
#else
    if (finished_) {
        return true;
    }
    
    if (process_id_ == -1) {
        return false;
    }
    
    pid_t child_pid = static_cast<pid_t>(process_id_);
    int status;
    
    pid_t result = waitpid(child_pid, &status, WNOHANG);
    
    if (result == -1) {
        throw RunnerException("Failed to check process status");
    } else if (result == 0) {
        return false;
    } else {
        finished_ = true;
        if (WIFEXITED(status)) {
            exit_code_ = WEXITSTATUS(status);
        } else {
            exit_code_ = -1;
        }
        return true;
    }
#endif
}

BackgroundProcess run(const std::string& command) {
    if (command.empty()) {
        throw RunnerException("Command is empty");
    }
    
#ifdef _WIN32
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    std::string full_cmd = "cmd.exe /c \"" + command + "\"";
    
    if (!CreateProcessA(nullptr, const_cast<char*>(full_cmd.c_str()), 
                        nullptr, nullptr, FALSE, 
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        throw RunnerException("Failed to create process");
    }
    
    CloseHandle(pi.hThread);
    
    return BackgroundProcess(pi.hProcess);
#else
    pid_t pid = fork();
    
    if (pid == -1) {
        throw RunnerException("Failed to fork process");
    }
    
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    
    return BackgroundProcess(static_cast<long>(pid));
#endif
}

} // namespace bg_runner