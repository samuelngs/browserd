#include "browserd/mcp/mcp_ipc_transport.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <string>

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/win/security_descriptor.h"
#include "base/win/win_util.h"

namespace browserd {
namespace {

constexpr DWORD kPipeBufferSize = 65536;
constexpr char kPipePrefix[] = R"(\\.\pipe\)";

HANDLE ToHandle(void* value) { return reinterpret_cast<HANDLE>(value); }

void* FromHandle(HANDLE value) { return reinterpret_cast<void*>(value); }

void CloseHandlePtr(void** value) {
  HANDLE handle = ToHandle(*value);
  if (handle && handle != INVALID_HANDLE_VALUE) {
    CancelIoEx(handle, nullptr);
    CloseHandle(handle);
  }
  *value = nullptr;
}

std::optional<std::wstring> NormalizePipeName(const std::string& path) {
  std::string name = path;
  if (base::StartsWith(name, kPipePrefix,
                       base::CompareCase::INSENSITIVE_ASCII)) {
    name = name.substr(strlen(kPipePrefix));
  }

  if (name.empty() || name.find_first_of("\\/") != std::string::npos) {
    LOG(ERROR) << "Invalid MCP IPC named pipe name: " << path;
    return std::nullopt;
  }

  return base::UTF8ToWide(std::string(kPipePrefix) + name);
}

std::optional<base::win::SecurityDescriptor::SelfRelative>
CreateCurrentUserPipeSecurity() {
  std::wstring user_sid;
  if (!base::win::GetUserSidString(&user_sid)) {
    PLOG(ERROR) << "Failed to get current user SID for MCP IPC pipe";
    return std::nullopt;
  }

  std::wstring sddl = L"D:P(A;;GA;;;" + user_sid + L")";
  std::optional<base::win::SecurityDescriptor> security_descriptor =
      base::win::SecurityDescriptor::FromSddl(sddl);
  if (!security_descriptor.has_value()) {
    PLOG(ERROR) << "Failed to create MCP IPC pipe security descriptor";
    return std::nullopt;
  }

  std::optional<base::win::SecurityDescriptor::SelfRelative> self_relative =
      security_descriptor->ToSelfRelative();
  if (!self_relative.has_value()) {
    PLOG(ERROR) << "Failed to convert MCP IPC pipe security descriptor";
    return std::nullopt;
  }

  return self_relative;
}

}  // namespace

void MCPIPCTransport::RunLoop(std::string path,
                              bool* success,
                              base::WaitableEvent* started) {
  std::optional<std::wstring> pipe_name = NormalizePipeName(path);
  if (!pipe_name.has_value()) {
    *success = false;
    started->Signal();
    return;
  }

  std::optional<base::win::SecurityDescriptor::SelfRelative> pipe_security =
      CreateCurrentUserPipeSecurity();
  if (!pipe_security.has_value()) {
    *success = false;
    started->Signal();
    return;
  }

  SECURITY_ATTRIBUTES security_attributes;
  security_attributes.nLength = sizeof(security_attributes);
  security_attributes.lpSecurityDescriptor = pipe_security->get();
  security_attributes.bInheritHandle = FALSE;

  bool signaled_start = false;

  while (!IsStopping()) {
    HANDLE pipe = CreateNamedPipeW(pipe_name->c_str(), PIPE_ACCESS_DUPLEX,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE |
                                       PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                   1, kPipeBufferSize, kPipeBufferSize, 0,
                                   &security_attributes);
    if (pipe == INVALID_HANDLE_VALUE) {
      PLOG(ERROR) << "Failed to create MCP IPC named pipe";
      if (!signaled_start) {
        *success = false;
        started->Signal();
      }
      return;
    }

    {
      base::AutoLock lock(lock_);
      active_client_handle_ = FromHandle(pipe);
      ++active_client_generation_;
    }

    if (!signaled_start) {
      std::cerr << "browserd MCP IPC listening on "
                << base::WideToUTF8(pipe_name.value()) << std::endl;
      *success = true;
      started->Signal();
      signaled_start = true;
    }

    BOOL connected = ConnectNamedPipe(pipe, nullptr)
                         ? TRUE
                         : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (!connected) {
      {
        base::AutoLock lock(lock_);
        if (active_client_handle_ == FromHandle(pipe)) {
          CloseHandlePtr(&active_client_handle_);
        } else {
          CloseHandle(pipe);
        }
      }
      continue;
    }

    std::string buffer;
    while (!IsStopping()) {
      uint64_t client_generation = 0;
      {
        base::AutoLock lock(lock_);
        client_generation = active_client_generation_;
      }

      char chunk[4096];
      DWORD bytes_read = 0;
      BOOL read_ok = ReadFile(pipe, chunk, sizeof(chunk), &bytes_read, nullptr);
      if (!read_ok || bytes_read == 0) {
        break;
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
      if (active_client_handle_ == FromHandle(pipe)) {
        DisconnectNamedPipe(pipe);
        CloseHandlePtr(&active_client_handle_);
      } else {
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
      }
    }
  }

  {
    base::AutoLock lock(lock_);
    RequestStopLocked();
  }
}

void MCPIPCTransport::RequestStopLocked() {
  CloseHandlePtr(&active_client_handle_);
}

void MCPIPCTransport::SendSerializedResponse(uint64_t client_generation,
                                             const std::string& serialized) {
  base::AutoLock lock(lock_);
  HANDLE pipe = ToHandle(active_client_handle_);
  if (!pipe || pipe == INVALID_HANDLE_VALUE ||
      active_client_generation_ != client_generation) {
    return;
  }

  const char* data = serialized.data();
  DWORD remaining = static_cast<DWORD>(
      std::min<size_t>(serialized.size(), std::numeric_limits<DWORD>::max()));
  while (remaining > 0) {
    DWORD written = 0;
    BOOL write_ok = WriteFile(pipe, data, remaining, &written, nullptr);
    if (!write_ok || written == 0) {
      CloseHandlePtr(&active_client_handle_);
      return;
    }
    data += written;
    remaining -= written;
  }

  FlushFileBuffers(pipe);
}

}  // namespace browserd
