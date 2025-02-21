#include "MainWindow.h"

// ImGui includes
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl.h"

#include "imgui.h"

#include <SDL2/SDL.h>

MainWindow::MainWindow(SDL_Window *window, SDL_GLContext glContext)
    : m_window(window), m_glContext(glContext) {
  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  // Setup Dear ImGui style (Dark theme)
  ImGui::StyleColorsDark();

  // Initialize ImGui for SDL and OpenGL
  ImGui_ImplSDL2_InitForOpenGL(window, glContext);
  ImGui_ImplOpenGL3_Init("#version 130"); // Adjust GLSL version as needed
}

MainWindow::~MainWindow() {
  // Cleanup ImGui resources
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
}

void MainWindow::processUI() {
  // Start the new ImGui frame
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame(m_window);
  ImGui::NewFrame();

  // Create a simple demo window
  ImGui::Begin("QuillArt - Vector Editor");
  ImGui::Text("Welcome to QuillArt!");
  ImGui::Text("This is your main UI window for editing vector art.");
  ImGui::End();

  // Render the ImGui frame
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
