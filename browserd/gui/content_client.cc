#include "browserd/gui/content_client.h"

#include "ui/base/resource/resource_bundle.h"

namespace browserd::gui {

ContentClient::ContentClient() = default;
ContentClient::~ContentClient() = default;

std::string_view ContentClient::GetDataResource(
    int resource_id,
    ui::ResourceScaleFactor scale_factor) {
  return ui::ResourceBundle::GetSharedInstance().GetRawDataResourceForScale(
      resource_id, scale_factor);
}

base::RefCountedMemory* ContentClient::GetDataResourceBytes(int resource_id) {
  return ui::ResourceBundle::GetSharedInstance().LoadDataResourceBytes(
      resource_id);
}

}  // namespace browserd::gui
