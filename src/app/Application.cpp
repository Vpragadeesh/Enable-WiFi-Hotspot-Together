#include "Application.h"
#include "/home/pragadeesh/quill-art/src/ui/MainWindow.h"
#include <iostream>
#include <stdexcept>

Application::Application()
    : window(nullptr), running(true), mainWindow(nullptr) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    throw std::runtime_error("Failed to initialize SDL: " +
                             std::string(SDL_GetError()));
  }

  // Create an SDL window
  window = SDL_CreateWindow("QuillArt - Vector Editor", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, 1280, 720,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
  if (!window) {
    throw std::runtime_error("Failed to create SDL window: " +
                             std::string(SDL_GetError()));
  }

  // Create an OpenGL context
  glContext = SDL_GL_CreateContext(window);
  if (!glContext) {
    throw std::runtime_error("Failed to create OpenGL context: " +
                             std::string(SDL_GetError()));
  }

  // Enable V-Sync for smoother rendering
  SDL_GL_SetSwapInterval(1);

  // Initialize MainWindow UI
  mainWindow = new MainWindow(window, glContext);

  std::cout << "Application initialized successfully.\n";
}

void Application::render() {
  // Clear screen
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // Draw a simple OpenGL triangle for testing
  glBegin(GL_TRIANGLES);
  glColor3f(1.0f, 0.0f, 0.0f);
  glVertex2f(0.0f, 0.5f);
  glColor3f(0.0f, 1.0f, 0.0f);
  glVertex2f(-0.5f, -0.5f);
  glColor3f(0.0f, 0.0f, 1.0f);
  glVertex2f(0.5f, -0.5f);
  glEnd();

  // Process and render the UI
  if (mainWindow) {
    mainWindow->processUI();
  }

  // Swap buffers
  SDL_GL_SwapWindow(window);
}

Application::~Application() {
  delete mainWindow;
  if (glContext) {
    SDL_GL_DeleteContext(glContext);
  }
  if (window) {
    SDL_DestroyWindow(window);
  }
  SDL_Quit();

  std::cout << "Application terminated successfully.\n";
}
