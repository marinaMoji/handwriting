#include "CursorFreeze.h"

#import <ApplicationServices/ApplicationServices.h>

namespace cursor_freeze {
void set(bool frozen) {
  CGAssociateMouseAndMouseCursorPosition(frozen ? false : true);
}
}  // namespace cursor_freeze
