#include "chat.h"

namespace axlora::app {

bool ChatApp::send(const char* destination, const char* message) {
  return mesh_.sendChat(destination, message);
}

}

