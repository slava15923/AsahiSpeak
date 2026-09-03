#pragma once
#include <CLI.hpp>
#include <string>
#include "network.hpp"

struct CLI_DATA {
    std::string username;
    std::string password;
    std::string ip;
    uint16_t port = 15923;
    std::string pathToCert;
    bool serverCertVerifi = false;
    const std::string version = VERSION;
}; 

CLI_DATA parseCli(int argc, char** argv) {
    CLI_DATA data;
    bool printVersion = false;
    CLI::App app{"Краткое описание вашего приложения"};

    app.add_flag("-v,--version", printVersion, "Включить подробный вывод");
    app.add_option("-u,--user", data.username, "Имя пользователя")->required();
    app.add_option("-p,--password", data.password, "Пароль")->required();
    app.add_option("-a,--address", data.ip, "IP-адрес сервера")->required();

    app.add_option("--port", data.port, "Порт сервера");
    app.add_option("--cert", data.pathToCert, "Путь к SSL-сертификату");
    app.add_flag("--no-verify", data.serverCertVerifi, "Отключить верификацию SSL-сертификата");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        exit(app.exit(e)); // Безопасный выход без return
    }

    if(printVersion) {
        std::cout << "AsahiSpeak версия: " << data.version << std::endl;
        //exit(0);
    }

    data.ip = resolve_ip_or_dns(data.ip);
    
    return data;
    
}