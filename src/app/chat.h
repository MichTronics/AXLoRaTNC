#pragma once

#include "mesh/relay.h"

namespace axlora::app {

class ChatApp {
 public:
  explicit ChatApp(mesh::MeshNode& mesh) : mesh_(mesh) {}
  bool send(const char* destination, const char* message);

 private:
  mesh::MeshNode& mesh_;
};

}

