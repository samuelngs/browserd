#include "browserd/mcp/mcp_transport.h"

#include <iostream>
#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"

namespace browserd {

std::optional<base::DictValue> ParseMCPTransportLine(
    const std::string& line) {
  if (line.empty()) {
    return std::nullopt;
  }

  auto parsed = base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC);
  if (!parsed.has_value()) {
    LOG(WARNING) << "Failed to parse MCP message: " << line;
    return std::nullopt;
  }

  return parsed;
}

std::optional<std::string> SerializeMCPTransportMessage(
    const base::DictValue& message) {
  std::string json;
  if (!base::JSONWriter::Write(message, &json)) {
    LOG(WARNING) << "Failed to serialize MCP message";
    return std::nullopt;
  }
  json.push_back('\n');
  return json;
}

MCPStdioTransport::MCPStdioTransport() : reader_thread_("MCPStdinReader") {}

MCPStdioTransport::~MCPStdioTransport() {
  reader_thread_.Stop();
}

void MCPStdioTransport::Start(
    scoped_refptr<base::SequencedTaskRunner> main_task_runner,
    MCPMessageCallback message_callback,
    CloseCallback close_callback) {
  main_task_runner_ = std::move(main_task_runner);
  message_callback_ = std::move(message_callback);
  close_callback_ = std::move(close_callback);

  reader_thread_.Start();
  reader_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&MCPStdioTransport::ReadLoop, base::Unretained(this)));
}

void MCPStdioTransport::SendResponse(
    std::optional<base::DictValue> response) {
  if (!response.has_value()) {
    return;
  }
  SendMessage(response.value());
}

void MCPStdioTransport::SendMessage(const base::DictValue& message) {
  std::optional<std::string> json = SerializeMCPTransportMessage(message);
  if (!json.has_value()) {
    return;
  }

  // MCP uses newline-delimited JSON on stdio.
  std::cout << json.value() << std::flush;
}

void MCPStdioTransport::ReadLoop() {
  std::string line;
  while (std::getline(std::cin, line)) {
    auto parsed = ParseMCPTransportLine(line);
    if (!parsed.has_value()) {
      continue;
    }

    main_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(message_callback_, std::move(parsed.value()),
                       base::BindOnce(&MCPStdioTransport::SendResponse,
                                      base::Unretained(this))));
  }

  if (close_callback_) {
    main_task_runner_->PostTask(FROM_HERE, std::move(close_callback_));
  }
}

}  // namespace browserd
