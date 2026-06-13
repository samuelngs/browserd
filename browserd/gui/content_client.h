#ifndef BROWSERD_GUI_CONTENT_CLIENT_H_
#define BROWSERD_GUI_CONTENT_CLIENT_H_

#include <string_view>

#include "content/public/common/content_client.h"

namespace browserd::gui {

class ContentClient : public content::ContentClient {
 public:
  ContentClient();
  ~ContentClient() override;

  ContentClient(const ContentClient&) = delete;
  ContentClient& operator=(const ContentClient&) = delete;

 private:
  std::string_view GetDataResource(
      int resource_id,
      ui::ResourceScaleFactor scale_factor) override;
  base::RefCountedMemory* GetDataResourceBytes(int resource_id) override;
};

}  // namespace browserd::gui

#endif  // BROWSERD_GUI_CONTENT_CLIENT_H_
