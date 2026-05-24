#include "browserd/mcp/mcp_transport.h"

#include <iostream>
#include <string>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"

namespace browserd {

MCPTransport::MCPTransport() : reader_thread_("MCPStdinReader") {}

MCPTransport::~MCPTransport() {
  reader_thread_.Stop();
}

void MCPTransport::Start(
    scoped_refptr<base::SequencedTaskRunner> main_task_runner,
    MessageCallback message_callback) {
  main_task_runner_ = std::move(main_task_runner);
  message_callback_ = std::move(message_callback);

  reader_thread_.Start();
  reader_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&MCPTransport::ReadLoop, base::Unretained(this)));
}

void MCPTransport::SendMessage(const base::DictValue& message) {
  std::string json;
  base::JSONWriter::Write(message, &json);
  // MCP uses newline-delimited JSON on stdio.
  std::cout << json << "\n" << std::flush;
}

void MCPTransport::ReadLoop() {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      continue;
    }

    auto parsed = base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC);
    if (!parsed.has_value()) {
      LOG(WARNING) << "Failed to parse MCP message: " << line;
      continue;
    }

    main_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(message_callback_, std::move(parsed.value())));
  }

  // stdin closed — shut down.
  main_task_runner_->PostTask(FROM_HERE, base::BindOnce([]() {
    LOG(INFO) << "MCP stdin closed, shutting down.";
  }));
}

}  // namespace browserd
