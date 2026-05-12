#include "log.h"

namespace axlora::util {
namespace {

bool enabled = true;

}

bool logEnabled() {
  return enabled;
}

void setLogEnabled(bool value) {
  enabled = value;
}

}
