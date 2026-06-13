#include "browserd/gui/mac_init.h"

#import <Cocoa/Cocoa.h>

#include "base/check.h"
#include "base/observer_list.h"

#import "base/mac/scoped_sending_event.h"
#import "base/message_loop/message_pump_apple.h"
#import "content/public/browser/native_event_processor_mac.h"
#import "content/public/browser/native_event_processor_observer_mac.h"

@interface BrowserdApplication
    : NSApplication <CrAppProtocol, CrAppControlProtocol, NativeEventProcessor>
@end

@implementation BrowserdApplication {
  base::ObserverList<content::NativeEventProcessorObserver>::Unchecked
      _observers;
  BOOL _handlingSendEvent;
}

- (BOOL)isHandlingSendEvent {
  return _handlingSendEvent;
}

- (void)setHandlingSendEvent:(BOOL)handlingSendEvent {
  _handlingSendEvent = handlingSendEvent;
}

- (void)sendEvent:(NSEvent*)event {
  base::mac::ScopedSendingEvent sendingEventScoper;
  content::ScopedNotifyNativeEventProcessorObserver scopedObserverNotifier(
      &self->_observers, event);
  [super sendEvent:event];
}

- (void)addNativeEventProcessorObserver:
    (content::NativeEventProcessorObserver*)observer {
  _observers.AddObserver(observer);
}

- (void)removeNativeEventProcessorObserver:
    (content::NativeEventProcessorObserver*)observer {
  _observers.RemoveObserver(observer);
}

@end

namespace browserd::gui {

void MacPreBrowserMain() {
  CHECK_EQ(NSApp, nil);
  [BrowserdApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
}

}  // namespace browserd::gui
