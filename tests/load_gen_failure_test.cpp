#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "codec.h"
#include "load_gen.h"
#include "protocol.h"
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <netinet/in.h>
#include <string>
#include <thread>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <sys/wait.h>

#define REQUIRE(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cerr << "[FAIL] " << __FILE__ << ':' << __LINE__ << ": " #condition << '\n'; \
            return false; \
        } \
    } while (false)

class TempDirectory
{
public:
    TempDirectory()
    {
        char pattern[] = "/tmp/lw-load-gen-failure-XXXXXX";
        char *path = ::mkdtemp(pattern);
        assert(path != nullptr);
        path_ = path;
    }

    ~TempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TempDirectory(const TempDirectory &) = delete;
    TempDirectory &operator=(const TempDirectory &) = delete;

    const std::string &path() const
    {
        return path_;
    }

private:
    std::string path_;
};

class ListeningSocket
{
public:
    ListeningSocket()
        :fd_(-1), port_(0)
    {
        fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        assert(fd_ != -1);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        assert(::bind(fd_, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0);
        assert(::listen(fd_, 8) == 0);

        socklen_t address_size = sizeof(address);
        assert(::getsockname(fd_, reinterpret_cast<sockaddr *>(&address), &address_size) == 0);
        port_ = ntohs(address.sin_port);
        assert(port_ != 0);
    }

    ~ListeningSocket()
    {
        if (fd_ != -1)
        {
            ::close(fd_);
        }
    }

    ListeningSocket(const ListeningSocket &) = delete;
    ListeningSocket &operator=(const ListeningSocket &) = delete;

    std::uint16_t port() const
    {
        return port_;
    }

private:
    int fd_;
    std::uint16_t port_;
};

class JoinErrorServer
{
public:
    JoinErrorServer()
        :fd_(-1), port_(0)
    {
        fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        assert(fd_ != -1);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        assert(::bind(
            fd_,
            reinterpret_cast<const sockaddr *>(&address),
            sizeof(address)) == 0);
        assert(::listen(fd_, 8) == 0);

        socklen_t address_size = sizeof(address);
        assert(::getsockname(
            fd_,
            reinterpret_cast<sockaddr *>(&address),
            &address_size) == 0);

        port_ = ntohs(address.sin_port);
        assert(port_ != 0);

        thread_ = std::thread(&JoinErrorServer::serve, this);
    }

    ~JoinErrorServer()
    {
        wait();

        if (fd_ != -1)
        {
            ::close(fd_);
        }
    }

    JoinErrorServer(const JoinErrorServer &) = delete;
    JoinErrorServer &operator=(const JoinErrorServer &) = delete;

    std::uint16_t port() const
    {
        return port_;
    }

    void wait()
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

private:
    void serve()
    {
        int client_fd = -1;

        do
        {
            client_fd = ::accept4(
                fd_,
                nullptr,
                nullptr,
                SOCK_CLOEXEC);
        } while (client_fd == -1 && errno == EINTR);

        assert(client_fd != -1);

        char request[4096];
        ssize_t read_size = -1;

        do
        {
            read_size = ::recv(
                client_fd,
                request,
                sizeof(request),
                0);
        } while (read_size == -1 && errno == EINTR);

        assert(read_size > 0);

        std::string payload;
        assert(Protocol::encode_error(
            MessageType::join,
            ErrorCode::room_not_found,
            payload));

        std::string frame;
        assert(Codec::encode(
            static_cast<std::uint16_t>(MessageType::error),
            payload,
            frame));

        std::size_t sent_size = 0;

        while (sent_size < frame.size())
        {
            const ssize_t write_size = ::send(
                client_fd,
                frame.data() + sent_size,
                frame.size() - sent_size,
                MSG_NOSIGNAL);

            if (write_size > 0)
            {
                sent_size += static_cast<std::size_t>(write_size);
                continue;
            }

            if (write_size == -1 && errno == EINTR)
            {
                continue;
            }

            assert(false);
        }

        assert(::close(client_fd) == 0);
    }

    int fd_;
    std::uint16_t port_;
    std::thread thread_;
};

class SocketCreationFailureEnvironment
{
public:
    SocketCreationFailureEnvironment()
    {
        assert(::getrlimit(RLIMIT_NOFILE, &original_) == 0);

        const int maximum_fd = maximum_open_file_descriptor();
        assert(maximum_fd >= 0);

        rlimit limited = original_;
        limited.rlim_cur = static_cast<rlim_t>(maximum_fd) + 2;
        assert(limited.rlim_cur <= original_.rlim_max);
        assert(::setrlimit(RLIMIT_NOFILE, &limited) == 0);

        while (true)
        {
            const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
            if (fd == -1)
            {
                assert(errno == EMFILE);
                break;
            }
            filler_fds_.push_back(fd);
        }

        assert(!filler_fds_.empty());

        const int one_available_fd = filler_fds_.back();
        filler_fds_.pop_back();
        assert(::close(one_available_fd) == 0);
    }

    ~SocketCreationFailureEnvironment()
    {
        for (const int fd : filler_fds_)
        {
            assert(::close(fd) == 0);
        }
        assert(::setrlimit(RLIMIT_NOFILE, &original_) == 0);
    }

    SocketCreationFailureEnvironment(
        const SocketCreationFailureEnvironment &) = delete;
    SocketCreationFailureEnvironment &operator=(
        const SocketCreationFailureEnvironment &) = delete;

private:
    static int maximum_open_file_descriptor()
    {
        DIR *directory = ::opendir("/proc/self/fd");
        assert(directory != nullptr);

        const int directory_fd = ::dirfd(directory);
        assert(directory_fd != -1);

        int maximum_fd = -1;
        while (dirent *entry = ::readdir(directory))
        {
            char *end = nullptr;
            errno = 0;
            const long fd = std::strtol(entry->d_name, &end, 10);
            if (errno == 0 &&
                end != entry->d_name &&
                *end == '\0' &&
                fd >= 0 &&
                fd != directory_fd &&
                fd > maximum_fd)
            {
                maximum_fd = static_cast<int>(fd);
            }
        }

        assert(::closedir(directory) == 0);
        return maximum_fd;
    }

    rlimit original_{};
    std::vector<int> filler_fds_;
};

static std::string read_file(const std::string &path)
{
    std::ifstream input(path, std::ios::in);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

static bool test_failed_run_records_and_reports_first_failure()
{
    ListeningSocket listener;
    TempDirectory report_directory;

    LoadGen::Config config;
    config.port = listener.port();
    config.client_count = 1;
    config.room_capacity = 1;
    config.connections_per_second = 100;
    config.connect_timeout_ms = 1000;
    config.join_timeout_ms = 50;
    config.move_interval_ms = 50;
    config.heartbeat_interval_ms = 50;
    config.warm_up_ms = 50;
    config.measure_ms = 50;
    config.drain_ms = 50;

    LoadGen load_gen(config);

    REQUIRE(load_gen.valid());
    REQUIRE(!load_gen.run());
    REQUIRE(load_gen.states() == LoadGen::States::failed);

    const LoadGen::Result &result = load_gen.result();

    REQUIRE(result.first_failure.recorded);
    REQUIRE(result.first_failure.state == LoadGen::States::ramp_up);
    REQUIRE(result.first_failure.reason == "join_timeout");
    REQUIRE(result.first_failure.has_client);
    REQUIRE(result.first_failure.client_index == 0);
    REQUIRE(result.first_failure.active_clients == 0);

    REQUIRE(load_gen.write_report(report_directory.path()));

    const std::string summary = read_file(
        report_directory.path() + "/summary.txt");

    REQUIRE(summary.find("state = failed\n") != std::string::npos);
    REQUIRE(summary.find("result.first_failure.recorded = true\n") != std::string::npos);
    REQUIRE(summary.find("result.first_failure.state = ramp_up\n") != std::string::npos);
    REQUIRE(summary.find("result.first_failure.reason = join_timeout\n") != std::string::npos);
    REQUIRE(summary.find("result.first_failure.has_client = true\n") != std::string::npos);
    REQUIRE(summary.find("result.first_failure.client_index = 0\n") != std::string::npos);
    REQUIRE(summary.find("result.first_failure.active_clients = 0\n") != std::string::npos);

    return true;
}

static bool test_socket_creation_failure_records_client_and_system_reason()
{
    LoadGen::Config config;
    config.port = 1;
    config.client_count = 1;
    config.room_capacity = 1;
    config.connections_per_second = 100;
    config.connect_timeout_ms = 1000;
    config.join_timeout_ms = 50;
    config.move_interval_ms = 50;
    config.heartbeat_interval_ms = 50;
    config.warm_up_ms = 50;
    config.measure_ms = 50;
    config.drain_ms = 50;

    LoadGen load_gen(config);
    REQUIRE(load_gen.valid());

    {
        SocketCreationFailureEnvironment environment;
        REQUIRE(!load_gen.run());
    }

    REQUIRE(load_gen.states() == LoadGen::States::failed);

    const LoadGen::Result &result = load_gen.result();
    const std::string reason_prefix =
        "socket: errno = " + std::to_string(EMFILE) + " ";

    REQUIRE(result.first_failure.recorded);
    REQUIRE(result.first_failure.state == LoadGen::States::ramp_up);
    REQUIRE(result.first_failure.reason.compare(
        0, reason_prefix.size(), reason_prefix) == 0);
    REQUIRE(result.first_failure.has_client);
    REQUIRE(result.first_failure.client_index == 0);
    REQUIRE(result.first_failure.active_clients == 0);

    return true;
}

static bool test_main_writes_report_after_failed_run()
{
    ListeningSocket listener;
    TempDirectory report_directory;

    const std::string port = std::to_string(listener.port());
    const pid_t child = ::fork();

    REQUIRE(child != -1);

    if (child == 0)
    {
        ::execl(
            "./build/load_gen",
            "load_gen",
            report_directory.path().c_str(),
            "127.0.0.1",
            port.c_str(),
            "1",
            "1",
            "100",
            static_cast<char *>(nullptr));

        ::_exit(127);
    }

    int process_status = 0;
    pid_t wait_result = -1;

    do
    {
        wait_result = ::waitpid(child, &process_status, 0);
    } while (wait_result == -1 && errno == EINTR);

    REQUIRE(wait_result == child);
    REQUIRE(WIFEXITED(process_status));
    REQUIRE(WEXITSTATUS(process_status) == 1);

    const std::string summary_path =
        report_directory.path() + "/summary.txt";

    REQUIRE(std::filesystem::exists(summary_path));

    const std::string summary = read_file(summary_path);

    REQUIRE(summary.find("state = failed\n") != std::string::npos);
    REQUIRE(summary.find(
        "result.first_failure.recorded = true\n") != std::string::npos);
    REQUIRE(summary.find(
        "result.first_failure.reason = join_timeout\n") != std::string::npos);

    return true;
}

static bool test_server_error_records_request_type_and_error_code()
{
    JoinErrorServer server;

    LoadGen::Config config;
    config.port = server.port();
    config.client_count = 1;
    config.room_capacity = 1;
    config.connections_per_second = 100;
    config.connect_timeout_ms = 1000;
    config.join_timeout_ms = 1000;
    config.move_interval_ms = 50;
    config.heartbeat_interval_ms = 50;
    config.warm_up_ms = 50;
    config.measure_ms = 50;
    config.drain_ms = 50;

    LoadGen load_gen(config);

    REQUIRE(load_gen.valid());

    const bool run_success = load_gen.run();
    server.wait();

    REQUIRE(!run_success);
    REQUIRE(load_gen.states() == LoadGen::States::failed);

    const LoadGen::Result &result = load_gen.result();

    REQUIRE(result.first_failure.recorded);
    REQUIRE(result.first_failure.state == LoadGen::States::ramp_up);
    REQUIRE(
        result.first_failure.reason ==
        "server_error: request_type=1 error_code=1");
    REQUIRE(result.first_failure.has_client);
    REQUIRE(result.first_failure.client_index == 0);
    REQUIRE(result.first_failure.active_clients == 0);
    REQUIRE(result.error_frames == 1);
    REQUIRE(result.join_failures == 1);
    REQUIRE(result.protocol_errors == 0);

    return true;
}

int main()
{
    std::cout << "[RUN] failed run records and reports first failure\n";

    if (!test_failed_run_records_and_reports_first_failure())
    {
        return 1;
    }

    std::cout << "[PASS] failed run records and reports first failure\n";
    std::cout << "[RUN] socket creation failure records client and system reason\n";

    if (!test_socket_creation_failure_records_client_and_system_reason())
    {
        return 1;
    }

    std::cout << "[PASS] socket creation failure records client and system reason\n";
    std::cout << "[RUN] server error records request type and error code\n";

    if (!test_server_error_records_request_type_and_error_code())
    {
        return 1;
    }

    std::cout << "[PASS] server error records request type and error code\n";
    std::cout << "[RUN] main writes report after failed run\n";

    if (!test_main_writes_report_after_failed_run())
    {
        return 1;
    }

    std::cout << "[PASS] main writes report after failed run\n";
    return 0;
}
