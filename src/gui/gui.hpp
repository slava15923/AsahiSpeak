#pragma once
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <string>
#include <thread>
#include "AudioTransmission.hpp"

class GUI {
private:
    std::string name;
    std::thread threadGUI;
    AudioTransmission& AT;

    static int startGUI(const std::string& name, AudioTransmission& AT);

    static void authScene(ImGuiIO& io, AudioTransmission& AT);
    
public:
    GUI(std::string name_, AudioTransmission& udp);
};
