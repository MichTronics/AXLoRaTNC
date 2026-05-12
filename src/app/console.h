#pragma once

#include <stddef.h>
#include <stdint.h>
#include "chat.h"
#include "mesh/relay.h"

namespace axlora::app {

class Console {
 public:
  Console(mesh::MeshNode& mesh, ChatApp& chat) : mesh_(mesh), chat_(chat) {}
  void begin();
  void loop();

 private:
  void handleLine(char* line);
  void printHelp() const;

  mesh::MeshNode& mesh_;
  ChatApp& chat_;
  char line_[256]{};
  size_t pos_ = 0;
};

}

