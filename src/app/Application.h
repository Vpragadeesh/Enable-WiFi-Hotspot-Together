#ifndef APPLICATION_H
#define APPLICATION_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

// Forward declaration of MainWindow
class MainWindow;

class Application {
public:
  Application();
  ~Application();

  void run();

private:
  void handleEvents();
  void render();

  SDL_Window *window;
  SDL_GLContext glContext;
  bool running;
  MainWindow *mainWindow; // Add this line
};

#endif // APPLICATION_H
