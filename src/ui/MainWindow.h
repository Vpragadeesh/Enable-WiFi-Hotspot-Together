#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

class MainWindow {
public:
  // Constructor: takes the SDL_Window and GL context created by Application
  MainWindow(SDL_Window *window, SDL_GLContext glContext);
  ~MainWindow();

  // Processes and renders the UI using Dear ImGui
  void processUI();

private:
  SDL_Window *m_window;
  SDL_GLContext m_glContext;
};

#endif // MAINWINDOW_H
