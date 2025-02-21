
#include "app/Application.h"
#include <iostream>

int main(int argc, char *argv[]) {
  try {
    Application app;
    app.run();
  } catch (const std::exception &ex) {
    std::cerr << "Exception: " << ex.what() << std::endl;
    return 1;
  }
  return 0;
}
