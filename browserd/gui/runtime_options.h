#ifndef BROWSERD_GUI_RUNTIME_OPTIONS_H_
#define BROWSERD_GUI_RUNTIME_OPTIONS_H_

namespace browserd::gui {

struct RuntimeOptions {
  bool create_initial_tab = true;
  bool shutdown_on_zero_tabs = true;
};

}  // namespace browserd::gui

#endif  // BROWSERD_GUI_RUNTIME_OPTIONS_H_
