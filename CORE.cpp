#include "ShoKuda_server.h"
#include <iostream>
#include <windows.h>
#include <thread>
#include <chrono>
#include <conio.h>  // ��� _kbhit() � _getch()
#include <iomanip>  // ��� setw()

std::atomic<bool> g_RunServer(true);

// ������� ������ ���� � ������ � ���������������
// �������������� IOCP_LogColor � ���� ������� Windows
WORD GetColorAttr(IOCP_LogColor color) {
    switch (color) {
    case IOCP_LogColor::GREEN:  return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case IOCP_LogColor::RED:    return FOREGROUND_RED | FOREGROUND_INTENSITY;
    case IOCP_LogColor::YELLOW: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case IOCP_LogColor::WHITE:  return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    default: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
}
// ������� ������ ������, ��������� � � ���� ������ width
void print_centered(const std::string& s, int width) {
    int len = static_cast<int>(s.length());
    if (len >= width) {
        // ���� ������ ������� ��� ����� ����, ������ ������� ��� ���� (��� �������)
        std::cout << s.substr(0, width);
    }
    else {
        int padding = width - len;
        int pad_left = padding / 2;
        int pad_right = padding - pad_left;
        std::cout << std::string(pad_left, ' ') << s << std::string(pad_right, ' ');
    }
}
void PrintColorLog(const IOCP_Log& log) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD colorAttr = GetColorAttr(log.getColor());
    SetConsoleTextAttribute(hConsole, colorAttr);

    std::cout << "["; print_centered(log.time, 23); std::cout << "] ";
    std::cout << "["; print_centered(log.type, 10); std::cout << "] ";
    std::cout << "["; print_centered(log.result, 8); std::cout << "] ";
    std::cout << "["; print_centered(log.message, 40); std::cout << "] ";
    std::cout << "["; print_centered(log.ip, 15); std::cout << "] ";
    std::cout << "["; print_centered(std::to_string(log.client_id),5); std::cout << "] ";
    std::cout << "\n";

    SetConsoleTextAttribute(hConsole, GetColorAttr(IOCP_LogColor::WHITE));
}
void PrintColorLogSQL(const Log& log) {


    std::cout << "["; print_centered(log.query, 23); std::cout << "] ";
    std::cout << "["; print_centered(log.result, 10); std::cout << "] ";
    std::cout << "["; print_centered(log.timestamp, 8); std::cout << "] ";
    std::cout << "\n";

}

int main()
{
    SetConsoleOutputCP(65001); // CP_UTF8

    ShoKuda_server server;

    try {
        // ������ �������
        if (server.Start_Server() != 0) {
            server.Log("START SERVER", "ERROR", "Failed start server", "0.0.0.0", 0);
            std::cerr << "Failed to start server.\n";
            return EXIT_FAILURE;
        }

        std::cout << u8"Server started. ������� ESC ��� ���������.\n";

        while (g_RunServer) {
            if (!server.logs_.empty())
            {
                std::cout << u8"\n===== ���� ������� =====\n";

                if (server.logs_.empty()) {
                    std::cout << "(���� �����)\n";
                }
                else {
                    // ������� ������ ���
                    for (const auto& log : server.logs_) {
                        PrintColorLog(log);
                    }
                }

                // �������� �� ESC
                if (_kbhit()) {
                    int key = _getch();
                    if (key == 27) { // ESC
                        g_RunServer = false;
                        break;
                    }
                }
                server.logs_.clear();
            }
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }

        std::cout << "������ ����������.\n";
    }
    catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return 0;
}
