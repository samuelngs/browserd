#include "browserd/mcp/mcp_ipc_transport.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/synchronization/waitable_event.h"

namespace browserd {
namespace {

constexpr int kListenBacklog = 4;
constexpr int kPollTimeoutMs = 100;
constexpr size_t kInvalidPollIndex = std::numeric_limits<size_t>::max();

void CloseFd(int* fd) {
  if (*fd >= 0) {
    close(*fd);
    *fd = -1;
  }
}

bool FillUnixAddress(const std::string& path,
                     sockaddr_un* address,
                     socklen_t* address_length) {
  if (path.empty() || path.size() >= sizeof(address->sun_path)) {
    LOG(ERROR) << "MCP IPC socket path is too long";
    return false;
  }

  *address = {};
  address->sun_family = AF_UNIX;
  base::span(address->sun_path)
      .first(path.size())
      .copy_from(base::span(path));
  base::span(address->sun_path)[path.size()] = '\0';
  *address_length =
      static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
  return true;
}

bool IsSafeParentDirectory(const base::FilePath& parent) {
  if (!base::DirectoryExists(parent)) {
    LOG(ERROR) << "MCP IPC parent directory does not exist: " << parent.value();
    return false;
  }

  struct stat stat_buffer;
  if (stat(parent.value().c_str(), &stat_buffer) != 0) {
    PLOG(ERROR) << "Failed to stat MCP IPC parent directory: "
                << parent.value();
    return false;
  }

  if (!S_ISDIR(stat_buffer.st_mode)) {
    LOG(ERROR) << "MCP IPC parent path is not a directory: " << parent.value();
    return false;
  }

  if (stat_buffer.st_uid != geteuid()) {
    LOG(ERROR) << "MCP IPC parent directory must be owned by the current user: "
               << parent.value();
    return false;
  }

  if ((stat_buffer.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
    LOG(ERROR) << "MCP IPC parent directory must not be group/other writable: "
               << parent.value();
    return false;
  }

  return true;
}

enum class ExistingSocketState {
  kNone,
  kActive,
  kStale,
  kInvalid,
  kError,
};

ExistingSocketState ProbeExistingSocket(const std::string& path) {
  struct stat stat_buffer;
  if (lstat(path.c_str(), &stat_buffer) != 0) {
    if (errno == ENOENT) {
      return ExistingSocketState::kNone;
    }
    PLOG(ERROR) << "Failed to inspect MCP IPC path: " << path;
    return ExistingSocketState::kError;
  }

  if (!S_ISSOCK(stat_buffer.st_mode)) {
    LOG(ERROR) << "MCP IPC path exists and is not a socket: " << path;
    return ExistingSocketState::kInvalid;
  }

  sockaddr_un address;
  socklen_t address_length = 0;
  if (!FillUnixAddress(path, &address, &address_length)) {
    return ExistingSocketState::kError;
  }

  int probe_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (probe_fd < 0) {
    PLOG(ERROR) << "Failed to create MCP IPC probe socket";
    return ExistingSocketState::kError;
  }

  int rv =
      connect(probe_fd, reinterpret_cast<sockaddr*>(&address), address_length);
  int connect_errno = errno;
  close(probe_fd);

  if (rv == 0) {
    LOG(ERROR) << "MCP IPC socket is already active: " << path;
    return ExistingSocketState::kActive;
  }

  if (connect_errno == ECONNREFUSED || connect_errno == ENOENT) {
    return ExistingSocketState::kStale;
  }

  errno = connect_errno;
  PLOG(ERROR) << "Failed to probe existing MCP IPC socket: " << path;
  return ExistingSocketState::kError;
}

int CreateListeningSocket(const std::string& path) {
  base::FilePath socket_path(path);
  if (!socket_path.IsAbsolute()) {
    LOG(ERROR) << "MCP IPC socket path must be absolute: " << path;
    return -1;
  }

  if (!IsSafeParentDirectory(socket_path.DirName())) {
    return -1;
  }

  sockaddr_un address;
  socklen_t address_length = 0;
  if (!FillUnixAddress(path, &address, &address_length)) {
    return -1;
  }

  ExistingSocketState existing = ProbeExistingSocket(path);
  switch (existing) {
    case ExistingSocketState::kNone:
      break;
    case ExistingSocketState::kStale:
      if (unlink(path.c_str()) != 0 && errno != ENOENT) {
        PLOG(ERROR) << "Failed to unlink stale MCP IPC socket: " << path;
        return -1;
      }
      break;
    case ExistingSocketState::kActive:
    case ExistingSocketState::kInvalid:
    case ExistingSocketState::kError:
      return -1;
  }

  int listener_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener_fd < 0) {
    PLOG(ERROR) << "Failed to create MCP IPC listener socket";
    return -1;
  }

  if (bind(listener_fd, reinterpret_cast<sockaddr*>(&address),
           address_length) != 0) {
    PLOG(ERROR) << "Failed to bind MCP IPC socket: " << path;
    close(listener_fd);
    return -1;
  }

  if (chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) {
    PLOG(WARNING) << "Failed to restrict MCP IPC socket permissions: " << path;
  }

  if (listen(listener_fd, kListenBacklog) != 0) {
    PLOG(ERROR) << "Failed to listen on MCP IPC socket: " << path;
    close(listener_fd);
    unlink(path.c_str());
    return -1;
  }

  return listener_fd;
}

void SetNoSigPipe(int fd) {
#ifdef SO_NOSIGPIPE
  int value = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value));
#else
  (void)fd;
#endif
}

}  // namespace

void MCPIPCTransport::RunLoop(std::string path,
                              bool* success,
                              base::WaitableEvent* started) {
  int listener_fd = CreateListeningSocket(path);
  if (listener_fd < 0) {
    *success = false;
    started->Signal();
    return;
  }

  {
    base::AutoLock lock(lock_);
    listener_fd_ = listener_fd;
    bound_socket_path_ = base::FilePath(path);
  }

  std::cerr << "browserd MCP IPC listening on " << path << std::endl;
  *success = true;
  started->Signal();

  std::string buffer;

  while (!IsStopping()) {
    int listener_snapshot = -1;
    int client_snapshot = -1;
    {
      base::AutoLock lock(lock_);
      listener_snapshot = listener_fd_;
      client_snapshot = active_client_fd_;
    }

    if (listener_snapshot < 0 && client_snapshot < 0) {
      break;
    }

    std::array<pollfd, 2> poll_fds = {};
    size_t poll_count = 0;
    size_t listener_poll_index = kInvalidPollIndex;
    size_t client_poll_index = kInvalidPollIndex;
    if (listener_snapshot >= 0) {
      listener_poll_index = poll_count;
      poll_fds[poll_count].fd = listener_snapshot;
      poll_fds[poll_count].events = POLLIN;
      ++poll_count;
    }
    if (client_snapshot >= 0) {
      client_poll_index = poll_count;
      poll_fds[poll_count].fd = client_snapshot;
      poll_fds[poll_count].events = POLLIN;
      ++poll_count;
    }

    int rv =
        poll(poll_fds.data(), static_cast<nfds_t>(poll_count), kPollTimeoutMs);
    if (rv < 0) {
      if (errno == EINTR || errno == EBADF) {
        continue;
      }
      PLOG(ERROR) << "MCP IPC poll failed";
      continue;
    }
    if (rv == 0) {
      continue;
    }

    auto is_ready = [](const pollfd& poll_fd) {
      return (poll_fd.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0;
    };

    const bool listener_ready = listener_poll_index != kInvalidPollIndex &&
                                is_ready(poll_fds[listener_poll_index]);
    const bool client_ready = client_poll_index != kInvalidPollIndex &&
                              is_ready(poll_fds[client_poll_index]);

    if (listener_snapshot >= 0 && listener_ready) {
      int accepted_fd = accept(listener_snapshot, nullptr, nullptr);
      if (accepted_fd >= 0) {
        SetNoSigPipe(accepted_fd);

        bool close_extra_client = false;
        {
          base::AutoLock lock(lock_);
          if (active_client_fd_ >= 0) {
            close_extra_client = true;
          } else {
            active_client_fd_ = accepted_fd;
            ++active_client_generation_;
            buffer.clear();
          }
        }

        if (close_extra_client) {
          close(accepted_fd);
        }
      } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK &&
                 !IsStopping()) {
        PLOG(ERROR) << "MCP IPC accept failed";
      }
    }

    client_snapshot = -1;
    uint64_t client_generation = 0;
    {
      base::AutoLock lock(lock_);
      client_snapshot = active_client_fd_;
      client_generation = active_client_generation_;
    }

    if (client_snapshot < 0 || !client_ready) {
      continue;
    }

    char chunk[4096];
    ssize_t bytes_read = recv(client_snapshot, chunk, sizeof(chunk), 0);
    if (bytes_read <= 0) {
      {
        base::AutoLock lock(lock_);
        if (active_client_fd_ == client_snapshot) {
          CloseFd(&active_client_fd_);
        }
      }
      buffer.clear();
      continue;
    }

    buffer.append(chunk, static_cast<size_t>(bytes_read));
    size_t newline_position = std::string::npos;
    while ((newline_position = buffer.find('\n')) != std::string::npos) {
      std::string line = buffer.substr(0, newline_position);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      buffer.erase(0, newline_position + 1);
      DispatchLine(std::move(line), client_generation);
    }
  }

  {
    base::AutoLock lock(lock_);
    RequestStopLocked();
  }
}

void MCPIPCTransport::RequestStopLocked() {
  CloseFd(&active_client_fd_);
  CloseFd(&listener_fd_);
  if (!bound_socket_path_.empty()) {
    unlink(bound_socket_path_.value().c_str());
    bound_socket_path_.clear();
  }
}

void MCPIPCTransport::SendSerializedResponse(uint64_t client_generation,
                                             const std::string& serialized) {
  base::AutoLock lock(lock_);
  if (active_client_fd_ < 0 || active_client_generation_ != client_generation) {
    return;
  }

  base::span<const char> remaining = base::span(serialized);
  while (!remaining.empty()) {
#ifdef MSG_NOSIGNAL
    constexpr int kSendFlags = MSG_NOSIGNAL;
#else
    constexpr int kSendFlags = 0;
#endif
    ssize_t sent =
        send(active_client_fd_, remaining.data(), remaining.size(), kSendFlags);
    if (sent <= 0) {
      if (errno == EINTR) {
        continue;
      }
      CloseFd(&active_client_fd_);
      return;
    }
    remaining = remaining.subspan(static_cast<size_t>(sent));
  }
}

}  // namespace browserd
