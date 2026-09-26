#include "gui/gui.hpp"

int GUI::startGUI(const std::string& name, AudioTransmission& AT) {
    if (!glfwInit())
        return 1;

    // 2. Настройка версии OpenGL (3.3) и создание окна
    const char* glsl_version = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // Только для macOS

    GLFWwindow* window = glfwCreateWindow(1280, 720, name.c_str(), nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Включить вертикальную синхронизацию (VSync)

    // 3. Инициализация контекста Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    
    // Настройка стиля (опционально)
    ImGui::StyleColorsDark();

    // 4. Инициализация бэкендов ImGui
    ImGui_ImplGlfw_InitForOpenGL(window, true); // true = установить коллбэки GLFW
    ImGui_ImplOpenGL3_Init(glsl_version);

    // 5. Настройка состояния для демонстрационных окон
    bool show_demo_window = true;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // 6. Главный цикл приложения
    while (!glfwWindowShouldClose(window))
    {
        // Обработка событий (клавиатура, мышь и т.д.)
        glfwPollEvents();

        // Запуск нового кадра ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);

        authScene(io, AT);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        // Смена буферов
        glfwSwapBuffers(window);
    }
    AT.stopTransmission();

    // 8. Очистка ресурсов
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

void GUI::authScene(ImGuiIO& io, AudioTransmission& AT) {
    static int channel = 0;
    static char ipbuffer[64] = "";
    static int  port = 15923;
    static char namebuffer[32] = "";
    static char passwordbuffer[32] = "";

    static float f = 1.0f;
    static int counter = 0;
    ImGui::Begin("Asahispeak");
            
    //ImGui::Text("AsahiSpeak");
    //ImGui::Checkbox("Demo Window", &show_demo_window);
    
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("volume", &f, 0.0f, 1.0f);
    //ImGui::ColorEdit3("clear color", (float*)&clear_color);
    ImGui::SameLine();
    ImGui::Text("counter = %d", counter);
            


    ImGui::InputText("IP", ipbuffer, sizeof(ipbuffer));
    ImGui::InputInt("Port", &port);

    ImGui::InputText("Username", namebuffer, sizeof(namebuffer));
    ImGui::InputText("Password", passwordbuffer, sizeof(passwordbuffer), ImGuiInputTextFlags_Password);
    ImGui::InputInt("Channel", &channel);
    if (ImGui::Button("Button")) AT.startTransmission(resolve_ip_or_dns(ipbuffer).c_str(), port, namebuffer, passwordbuffer, channel);

    ImGui::Text("%.1f FPS", io.Framerate);
    ImGui::End();
}

GUI::GUI(std::string name_, AudioTransmission& udp) : AT(udp) {
    name = name_;

    threadGUI = std::thread(startGUI, name, std::ref(AT));
    threadGUI.detach();
}