#ifndef BROWSERD_CORE_BROWSER_RESULT_H_
#define BROWSERD_CORE_BROWSER_RESULT_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "browserd/browser_runtime.h"

namespace browserd {

enum class BrowserStatusCode {
  kOk = 0,
  kInvalidArgument = 1,
  kSessionNotReady = 2,
  kTabNotFound = 3,
  kNoActiveFrame = 4,
  kNavigationFailed = 5,
  kScreenshotFailed = 6,
  kInternalError = 7,
  kTimeout = 8,
  kNotFound = 9,
};

struct BrowserStatus {
  BrowserStatus();
  BrowserStatus(const BrowserStatus& other);
  BrowserStatus& operator=(const BrowserStatus& other);
  BrowserStatus(BrowserStatus&& other);
  BrowserStatus& operator=(BrowserStatus&& other);
  BrowserStatus(BrowserStatusCode code, std::string message);
  ~BrowserStatus();

  BrowserStatusCode code = BrowserStatusCode::kOk;
  std::string message;

  bool ok() const { return code == BrowserStatusCode::kOk; }

  static BrowserStatus Ok();
  static BrowserStatus Error(BrowserStatusCode code, std::string message);
};

template <typename T>
struct BrowserResult {
  BrowserStatus status;
  std::optional<T> value;

  bool ok() const { return status.ok(); }

  static BrowserResult<T> Ok(T value) {
    BrowserResult<T> result;
    result.status = BrowserStatus::Ok();
    result.value = std::move(value);
    return result;
  }

  static BrowserResult<T> Error(BrowserStatusCode code, std::string message) {
    BrowserResult<T> result;
    result.status = BrowserStatus::Error(code, std::move(message));
    return result;
  }
};

struct EvaluateResult {
  EvaluateResult();
  EvaluateResult(const EvaluateResult& other);
  EvaluateResult& operator=(const EvaluateResult& other);
  EvaluateResult(EvaluateResult&& other);
  EvaluateResult& operator=(EvaluateResult&& other);
  ~EvaluateResult();

  enum class Kind {
    kJson,
    kUndefined,
  };

  Kind kind = Kind::kUndefined;
  std::string json;
};

struct CookieInfo {
  CookieInfo();
  CookieInfo(const CookieInfo& other);
  CookieInfo& operator=(const CookieInfo& other);
  CookieInfo(CookieInfo&& other);
  CookieInfo& operator=(CookieInfo&& other);
  ~CookieInfo();

  std::string name;
  std::string value;
  std::string domain;
  std::string path;
  bool secure = false;
  bool http_only = false;
  double expires_unix_seconds = 0.0;
};

struct RefOptions {
  RefOptions();
  RefOptions(const RefOptions& other);
  RefOptions& operator=(const RefOptions& other);
  RefOptions(RefOptions&& other);
  RefOptions& operator=(RefOptions&& other);
  ~RefOptions();

  std::string ref;
};

struct TypeOptions {
  TypeOptions();
  TypeOptions(const TypeOptions& other);
  TypeOptions& operator=(const TypeOptions& other);
  TypeOptions(TypeOptions&& other);
  TypeOptions& operator=(TypeOptions&& other);
  ~TypeOptions();

  std::string ref;
  std::string text;
  bool clear = false;
};

struct KeyOptions {
  KeyOptions();
  KeyOptions(const KeyOptions& other);
  KeyOptions& operator=(const KeyOptions& other);
  KeyOptions(KeyOptions&& other);
  KeyOptions& operator=(KeyOptions&& other);
  ~KeyOptions();

  std::string key;
};

struct SelectOptions {
  SelectOptions();
  SelectOptions(const SelectOptions& other);
  SelectOptions& operator=(const SelectOptions& other);
  SelectOptions(SelectOptions&& other);
  SelectOptions& operator=(SelectOptions&& other);
  ~SelectOptions();

  std::string ref;
  std::vector<std::string> values;
};

struct DragOptions {
  DragOptions();
  DragOptions(const DragOptions& other);
  DragOptions& operator=(const DragOptions& other);
  DragOptions(DragOptions&& other);
  DragOptions& operator=(DragOptions&& other);
  ~DragOptions();

  std::string start_ref;
  std::string end_ref;
};

struct ScrollOptions {
  ScrollOptions();
  ScrollOptions(const ScrollOptions& other);
  ScrollOptions& operator=(const ScrollOptions& other);
  ScrollOptions(ScrollOptions&& other);
  ScrollOptions& operator=(ScrollOptions&& other);
  ~ScrollOptions();

  int delta_x = 0;
  int delta_y = 0;
};

struct WaitOptions {
  WaitOptions();
  WaitOptions(const WaitOptions& other);
  WaitOptions& operator=(const WaitOptions& other);
  WaitOptions(WaitOptions&& other);
  WaitOptions& operator=(WaitOptions&& other);
  ~WaitOptions();

  std::optional<std::string> selector;
  std::optional<std::string> text;
  int timeout_ms = 30000;
};

struct CookieListOptions {
  CookieListOptions();
  CookieListOptions(const CookieListOptions& other);
  CookieListOptions& operator=(const CookieListOptions& other);
  CookieListOptions(CookieListOptions&& other);
  CookieListOptions& operator=(CookieListOptions&& other);
  ~CookieListOptions();

  std::optional<std::string> url;
};

struct CookieGetOptions {
  CookieGetOptions();
  CookieGetOptions(const CookieGetOptions& other);
  CookieGetOptions& operator=(const CookieGetOptions& other);
  CookieGetOptions(CookieGetOptions&& other);
  CookieGetOptions& operator=(CookieGetOptions&& other);
  ~CookieGetOptions();

  std::string name;
};

struct CookieSetOptions {
  CookieSetOptions();
  CookieSetOptions(const CookieSetOptions& other);
  CookieSetOptions& operator=(const CookieSetOptions& other);
  CookieSetOptions(CookieSetOptions&& other);
  CookieSetOptions& operator=(CookieSetOptions&& other);
  ~CookieSetOptions();

  std::string name;
  std::string value;
  std::optional<std::string> domain;
  std::optional<std::string> path;
  std::optional<std::string> url;
  bool secure = false;
  bool http_only = false;
};

struct CookieDeleteOptions {
  CookieDeleteOptions();
  CookieDeleteOptions(const CookieDeleteOptions& other);
  CookieDeleteOptions& operator=(const CookieDeleteOptions& other);
  CookieDeleteOptions(CookieDeleteOptions&& other);
  CookieDeleteOptions& operator=(CookieDeleteOptions&& other);
  ~CookieDeleteOptions();

  std::string name;
  std::optional<std::string> url;
  std::optional<std::string> domain;
};

}  // namespace browserd

#endif  // BROWSERD_CORE_BROWSER_RESULT_H_
