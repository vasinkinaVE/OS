#ifndef BACKGROUND_RUNNER_HPP
#define BACKGROUND_RUNNER_HPP

#include <string>
#include <stdexcept>

namespace bg_runner {

class RunnerException : public std::runtime_error {
public:
    explicit RunnerException(const std::string& message) : std::runtime_error(message) {}
};

class BackgroundProcess {
private:
#ifdef _WIN32
    void* process_handle_;
#else
    long process_id_;
    mutable int exit_code_;  
    mutable bool finished_;  
#endif
    
public:
    BackgroundProcess() {
#ifdef _WIN32
        process_handle_ = nullptr;
#else
        process_id_ = -1;
        exit_code_ = 0;
        finished_ = false;
#endif
    }
    
#ifdef _WIN32
    explicit BackgroundProcess(void* handle) : process_handle_(handle) {}
#else
    explicit BackgroundProcess(long pid) : process_id_(pid), exit_code_(0), finished_(false) {}
#endif
    
    
    ~BackgroundProcess() = default;
    
    BackgroundProcess(const BackgroundProcess&) = delete;
    BackgroundProcess& operator=(const BackgroundProcess&) = delete;
    
    BackgroundProcess(BackgroundProcess&& other) noexcept {
#ifdef _WIN32
        process_handle_ = other.process_handle_;
        other.process_handle_ = nullptr;
#else
        process_id_ = other.process_id_;
        exit_code_ = other.exit_code_;
        finished_ = other.finished_;
        other.process_id_ = -1;
        other.exit_code_ = 0;
        other.finished_ = false;
#endif
    }
    
    BackgroundProcess& operator=(BackgroundProcess&& other) noexcept {
        if (this != &other) {
#ifdef _WIN32
            process_handle_ = other.process_handle_;
            other.process_handle_ = nullptr;
#else
            process_id_ = other.process_id_;
            exit_code_ = other.exit_code_;
            finished_ = other.finished_;
            other.process_id_ = -1;
            other.exit_code_ = 0;
            other.finished_ = false;
#endif
        }
        return *this;
    }
    
    bool valid() const {
#ifdef _WIN32
        return process_handle_ != nullptr;
#else
        return process_id_ != -1 || finished_;
#endif
    }
    
    int wait();
    
    bool is_finished() const;
};

BackgroundProcess run(const std::string& command);

} // namespace bg_runner

#endif // BACKGROUND_RUNNER_HPP