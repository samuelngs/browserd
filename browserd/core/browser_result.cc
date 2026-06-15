#include "browserd/core/browser_result.h"

#include <utility>

namespace browserd {

BrowserStatus::BrowserStatus() = default;
BrowserStatus::BrowserStatus(const BrowserStatus& other) = default;
BrowserStatus& BrowserStatus::operator=(const BrowserStatus& other) = default;
BrowserStatus::BrowserStatus(BrowserStatus&& other) = default;
BrowserStatus& BrowserStatus::operator=(BrowserStatus&& other) = default;

BrowserStatus::BrowserStatus(BrowserStatusCode code, std::string message)
    : code(code), message(std::move(message)) {}

BrowserStatus::~BrowserStatus() = default;

BrowserStatus BrowserStatus::Ok() {
  return BrowserStatus();
}

BrowserStatus BrowserStatus::Error(BrowserStatusCode code,
                                   std::string message) {
  return BrowserStatus(code, std::move(message));
}

EvaluateResult::EvaluateResult() = default;
EvaluateResult::EvaluateResult(const EvaluateResult& other) = default;
EvaluateResult& EvaluateResult::operator=(const EvaluateResult& other) =
    default;
EvaluateResult::EvaluateResult(EvaluateResult&& other) = default;
EvaluateResult& EvaluateResult::operator=(EvaluateResult&& other) = default;
EvaluateResult::~EvaluateResult() = default;

CookieInfo::CookieInfo() = default;
CookieInfo::CookieInfo(const CookieInfo& other) = default;
CookieInfo& CookieInfo::operator=(const CookieInfo& other) = default;
CookieInfo::CookieInfo(CookieInfo&& other) = default;
CookieInfo& CookieInfo::operator=(CookieInfo&& other) = default;
CookieInfo::~CookieInfo() = default;

RefOptions::RefOptions() = default;
RefOptions::RefOptions(const RefOptions& other) = default;
RefOptions& RefOptions::operator=(const RefOptions& other) = default;
RefOptions::RefOptions(RefOptions&& other) = default;
RefOptions& RefOptions::operator=(RefOptions&& other) = default;
RefOptions::~RefOptions() = default;

TypeOptions::TypeOptions() = default;
TypeOptions::TypeOptions(const TypeOptions& other) = default;
TypeOptions& TypeOptions::operator=(const TypeOptions& other) = default;
TypeOptions::TypeOptions(TypeOptions&& other) = default;
TypeOptions& TypeOptions::operator=(TypeOptions&& other) = default;
TypeOptions::~TypeOptions() = default;

KeyOptions::KeyOptions() = default;
KeyOptions::KeyOptions(const KeyOptions& other) = default;
KeyOptions& KeyOptions::operator=(const KeyOptions& other) = default;
KeyOptions::KeyOptions(KeyOptions&& other) = default;
KeyOptions& KeyOptions::operator=(KeyOptions&& other) = default;
KeyOptions::~KeyOptions() = default;

SelectOptions::SelectOptions() = default;
SelectOptions::SelectOptions(const SelectOptions& other) = default;
SelectOptions& SelectOptions::operator=(const SelectOptions& other) = default;
SelectOptions::SelectOptions(SelectOptions&& other) = default;
SelectOptions& SelectOptions::operator=(SelectOptions&& other) = default;
SelectOptions::~SelectOptions() = default;

DragOptions::DragOptions() = default;
DragOptions::DragOptions(const DragOptions& other) = default;
DragOptions& DragOptions::operator=(const DragOptions& other) = default;
DragOptions::DragOptions(DragOptions&& other) = default;
DragOptions& DragOptions::operator=(DragOptions&& other) = default;
DragOptions::~DragOptions() = default;

ScrollOptions::ScrollOptions() = default;
ScrollOptions::ScrollOptions(const ScrollOptions& other) = default;
ScrollOptions& ScrollOptions::operator=(const ScrollOptions& other) = default;
ScrollOptions::ScrollOptions(ScrollOptions&& other) = default;
ScrollOptions& ScrollOptions::operator=(ScrollOptions&& other) = default;
ScrollOptions::~ScrollOptions() = default;

WaitOptions::WaitOptions() = default;
WaitOptions::WaitOptions(const WaitOptions& other) = default;
WaitOptions& WaitOptions::operator=(const WaitOptions& other) = default;
WaitOptions::WaitOptions(WaitOptions&& other) = default;
WaitOptions& WaitOptions::operator=(WaitOptions&& other) = default;
WaitOptions::~WaitOptions() = default;

CookieListOptions::CookieListOptions() = default;
CookieListOptions::CookieListOptions(const CookieListOptions& other) = default;
CookieListOptions& CookieListOptions::operator=(
    const CookieListOptions& other) = default;
CookieListOptions::CookieListOptions(CookieListOptions&& other) = default;
CookieListOptions& CookieListOptions::operator=(CookieListOptions&& other) =
    default;
CookieListOptions::~CookieListOptions() = default;

CookieGetOptions::CookieGetOptions() = default;
CookieGetOptions::CookieGetOptions(const CookieGetOptions& other) = default;
CookieGetOptions& CookieGetOptions::operator=(const CookieGetOptions& other) =
    default;
CookieGetOptions::CookieGetOptions(CookieGetOptions&& other) = default;
CookieGetOptions& CookieGetOptions::operator=(CookieGetOptions&& other) =
    default;
CookieGetOptions::~CookieGetOptions() = default;

CookieSetOptions::CookieSetOptions() = default;
CookieSetOptions::CookieSetOptions(const CookieSetOptions& other) = default;
CookieSetOptions& CookieSetOptions::operator=(const CookieSetOptions& other) =
    default;
CookieSetOptions::CookieSetOptions(CookieSetOptions&& other) = default;
CookieSetOptions& CookieSetOptions::operator=(CookieSetOptions&& other) =
    default;
CookieSetOptions::~CookieSetOptions() = default;

CookieDeleteOptions::CookieDeleteOptions() = default;
CookieDeleteOptions::CookieDeleteOptions(const CookieDeleteOptions& other) =
    default;
CookieDeleteOptions& CookieDeleteOptions::operator=(
    const CookieDeleteOptions& other) = default;
CookieDeleteOptions::CookieDeleteOptions(CookieDeleteOptions&& other) =
    default;
CookieDeleteOptions& CookieDeleteOptions::operator=(
    CookieDeleteOptions&& other) = default;
CookieDeleteOptions::~CookieDeleteOptions() = default;

}  // namespace browserd
