#pragma once

#include <winsock2.h>
#include <Windows.h>
#include <WS2tcpip.h>
#include <unordered_map>
#include <vector>
#include <stack>
#include <mutex>
#include <thread>
#include <chrono>
#include <memory>
#include <string>
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")

//
// ==== Структуры и перечисления ====
//

enum OperationType { RECV, SEND };

enum IOCP_LogColor {
    GREEN,   // Успешные операции
    RED,     // Ошибки или неудачные операции
    YELLOW,  // Предупреждения
    WHITE    // Нейтральные
};

struct IOCP_Client {
    SOCKET _mainSocket;
    std::string _ip;

    size_t expect_bytes;

    std::vector<uint8_t> recv_buffer;
    std::vector<uint8_t> recv_full;
    bool is_disconnected = false;
    std::chrono::steady_clock::time_point last_active_time;

    IOCP_Client() {
        expect_bytes = 0;

        recv_buffer.reserve(4096);
        recv_full.reserve(10000);
        last_active_time = std::chrono::steady_clock::now();
    }
    virtual ~IOCP_Client() = default;
};

struct IOCP_Log {
    std::string time;
    std::string type;     // "SEND", "RECV", "ERROR" и т.д.
    std::string result;   // "SUCCESS", "FAILED", "WARN"
    std::string message;
    std::string ip;
    int client_id;

    IOCP_LogColor getColor() const {
        if (result == "SUCCESS") return IOCP_LogColor::GREEN;
        if (result == "FAILED")  return IOCP_LogColor::RED;
        if (result == "WARN")    return IOCP_LogColor::YELLOW;
        return IOCP_LogColor::WHITE;
    }
};

// Операция IOCP (не шаблон! чтобы не мучаться с лишними <>)
struct IOCP_Operation {
    OVERLAPPED     overlapped;
    WSABUF         buffer;
    uint8_t        data[5012];
    OperationType  type;
    std::shared_ptr<IOCP_Client> client; // shared_ptr к базовому клиенту

    IOCP_Operation() {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
        buffer.buf = reinterpret_cast<CHAR*>(data);
        buffer.len = sizeof(data);
        type = OperationType::RECV;
    }
};

//
// ==== Класс IOCP_Server (шаблон по ClientType) ====
//
template<typename ClientType = IOCP_Client>class IOCP_Server {
public:
    IOCP_Server(unsigned int port) : _port(port) {}
    virtual ~IOCP_Server() { Stop_Server(); }

    //
    // 1) Хранилище клиентов (shared_ptr<ClientType>)
    //
    std::unordered_map<SOCKET, std::shared_ptr<ClientType>> clients_;
    std::mutex clients_mutex_;

    //
    // 2) Пул операций (IOCP_Operation*)
    //
    std::stack<IOCP_Operation*> operation_pool_;
    std::mutex operation_pool_mutex_;

    //
    // 3) Логи
    //
    std::vector<IOCP_Log> logs_;
    std::mutex log_mutex_;

    //
    // 4) Таймаут-чекер
    //
    std::thread timeout_thread_;
    std::atomic<bool> timeout_thread_running_{ false };

    //
    // 5) Флаг остановки сервера
    //
    std::atomic<bool> shutdown_requested_{ false };

public:
    //
    // ======= Запуск/остановка сервера =======
    //

    int Start_Server() {
        // 1) Инициализируем Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            Log("WSAStartup", "FAILED", "WSAStartup failed", "0.0.0.0", -1);
            return -1;
        }

        // 2) Создаем слушающий сокет (Overlapped)
        SOCKET listenSocket = WSASocketW(
            AF_INET, SOCK_STREAM, IPPROTO_TCP,
            nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (listenSocket == INVALID_SOCKET) {
            Log("WSASocket", "FAILED", "Failed to create listening socket", "0.0.0.0", -1);
            WSACleanup();
            return -1;
        }

        // 3) Настройка адреса
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(_port);

        // 4) Привязываем
        if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            Log("BIND", "FAILED", "bind() failed", "0.0.0.0", -1);
            closesocket(listenSocket);
            WSACleanup();
            return -1;
        }

        // 5) Слушаем
        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            Log("LISTEN", "FAILED", "listen() failed", "0.0.0.0", -1);
            closesocket(listenSocket);
            WSACleanup();
            return -1;
        }

        // 6) Создаем IOCP-порт
        iocp_port = CreateIoCompletionPort(
            INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (iocp_port == nullptr) {
            Log("IOCP", "FAILED", "CreateIoCompletionPort failed", "0.0.0.0", -1);
            closesocket(listenSocket);
            WSACleanup();
            return -1;
        }
        Log("IOCP", "SUCCESS", "IOCP port created", "0.0.0.0", -1);

        // 7) Предварительно создаем операции (pool)
        const size_t prealloc_ops = 256;
        {
            std::lock_guard<std::mutex> lock(operation_pool_mutex_);
            for (size_t i = 0; i < prealloc_ops; ++i) {
                operation_pool_.push(new IOCP_Operation());
            }
        }
        Log("POOL", "SUCCESS", "Operation pool initialized", "0.0.0.0", -1);

        // 8) Запускаем worker-потоки
        unsigned int threadsCount = std::thread::hardware_concurrency();
        if (threadsCount > 1) threadsCount -= 1;
        for (unsigned int i = 0; i < threadsCount; ++i) {
            std::thread(&IOCP_Server::Worker_Thread, this).detach();
        }
        Log("THREADS", "SUCCESS", "Worker threads launched", "0.0.0.0", -1);

        // 9) Запускаем поток, принимающий новых клиентов
        std::thread([this, listenSocket]() {
            while (!shutdown_requested_.load()) {
                Accept_Client(listenSocket);
            }
            closesocket(listenSocket);
            }).detach();

            // 10) Запускаем таймаут-чекер
            timeout_thread_running_ = true;
            timeout_thread_ = std::thread(&IOCP_Server::TimeoutCheckerThread, this);

            Log("START", "SUCCESS", "Server started successfully on port " + std::to_string(_port), "0.0.0.0", -1);
            return 0;
    }

    int Stop_Server() {
        shutdown_requested_ = true;
        Log("STOP", "SUCCESS", "Stopping server", "0.0.0.0", -1);

        // 1) Закрываем все клиентские сокеты
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (auto& kv : clients_) {
                SafeCloseSocket(kv.second->_mainSocket);
                Log("CLIENT", "CLOSED", "Client socket closed", kv.second->_ip, static_cast<int>(kv.first));
            }
            clients_.clear();
            Log("CLIENT", "SUCCESS", "All client connections cleared", "0.0.0.0", -1);
        }

        // 2) Закрываем IOCP-порт
        if (iocp_port != nullptr) {
            CloseHandle(iocp_port);
            iocp_port = nullptr;
            Log("IOCP", "SUCCESS", "IOCP port closed", "0.0.0.0", -1);
        }

        // 3) Останавливаем таймаут-чекер
        timeout_thread_running_ = false;
        if (timeout_thread_.joinable())
            timeout_thread_.join();

        // 4) WSACleanup
        WSACleanup();
        Log("WSA", "SUCCESS", "WSACleanup called", "0.0.0.0", -1);

        return 0;
    }

protected:
    //
    // ======= Приём и удаление клиента =======
    //

    int Accept_Client(SOCKET listenSocket) {
        sockaddr_in clientAddr;
        int addrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientSocket == INVALID_SOCKET) {
            Log("ACCEPT", "FAILED", "accept() failed", "0.0.0.0", -1);
            return -1;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);

        auto clientPtr = std::make_shared<ClientType>();
        clientPtr->_mainSocket = clientSocket;
        clientPtr->_ip = clientIP;
        clientPtr->recv_buffer.clear();
        clientPtr->is_disconnected = false;
        clientPtr->last_active_time = std::chrono::steady_clock::now();

        // Привязываем сокет к IOCP (ключ = clientPtr.get())
        if (CreateIoCompletionPort(
            (HANDLE)clientSocket,
            iocp_port,
            reinterpret_cast<ULONG_PTR>(clientPtr.get()),
            0) == nullptr) {
            Log("IOCP_BIND", "FAILED", "CreateIoCompletionPort() failed", clientIP, -1);
            SafeCloseSocket(clientPtr->_mainSocket);
            return -1;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_[clientSocket] = clientPtr;
        }

        // Запускаем асинхронный приём
        if (AsyncRecv(clientPtr) != 0) {
            Log("RECV", "FAILED", "AsyncRecv() failed", clientIP, -1);
            Delete_Client(clientSocket);
            return -1;
        }

        Log("ACCEPT", "SUCCESS", "Client connected", clientIP, static_cast<int>(clientSocket));
        return 0;
    }

    int Delete_Client(SOCKET clientSocket) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.find(clientSocket);
        if (it != clients_.end()) {
            std::string msg = "Deleting client (Socket: "
                + std::to_string(clientSocket)
                + ", IP: " + it->second->_ip + ")";
            Log("DELETE_CLIENT", "INFO", msg, it->second->_ip, static_cast<int>(clientSocket));

            SafeCloseSocket(it->second->_mainSocket);
            it->second->is_disconnected = true;
            clients_.erase(it);
            return 0;
        }
        return -1;
    }

    //
    // ======= Получение/возврат IOCP_Operation =======
    //

    IOCP_Operation* GetOperation(OperationType type, std::shared_ptr<ClientType> clientPtr) {
        std::lock_guard<std::mutex> lock(operation_pool_mutex_);
        IOCP_Operation* op = nullptr;
        if (!operation_pool_.empty()) {
            op = operation_pool_.top();
            operation_pool_.pop();
            ZeroMemory(op, sizeof(IOCP_Operation));
        }
        else {
            op = new IOCP_Operation();
        }

        op->type = type;
        // Сохраняем shared_ptr<IOCP_Client> (приводим shared_ptr<ClientType> к shared_ptr<IOCP_Client>)
        std::shared_ptr<IOCP_Client> basePtr = std::static_pointer_cast<IOCP_Client>(clientPtr);
        op->client = basePtr;

        op->buffer.buf = reinterpret_cast<CHAR*>(op->data);
        op->buffer.len = sizeof(op->data);
        return op;
    }

    void ReturnOperation(IOCP_Operation* op) {
        if (op == nullptr) return;
        // Убираем reference на клиента
        op->client.reset();
        std::lock_guard<std::mutex> lock(operation_pool_mutex_);
        operation_pool_.push(op);
    }

    //
    // ======= AsyncRecv / AsyncSend =======
    //

    int AsyncRecv(std::shared_ptr<ClientType> clientPtr) {
        // Получаем новую операцию
        IOCP_Operation* op = GetOperation(OperationType::RECV, clientPtr);

        DWORD flags = 0;
        // Правильная 7-аргументная сигнатура WSARecv
        if (WSARecv(
            clientPtr->_mainSocket,
            &op->buffer,
            1,
            nullptr,
            &flags,
            &op->overlapped,
            nullptr) == SOCKET_ERROR)
        {
            int error = WSAGetLastError();
            if (error != WSA_IO_PENDING) {
                ReturnOperation(op);
                return -1;
            }
        }
        return 0;
    }

    int AsyncSend(std::shared_ptr<ClientType> clientPtr, const std::vector<uint8_t>& data) {
        if (data.empty()) return 0;
        IOCP_Operation* op = GetOperation(OperationType::SEND, clientPtr);

        size_t bytes_to_send = min(data.size(), sizeof(op->data));
        memcpy(op->data, data.data(), bytes_to_send);
        op->buffer.len = static_cast<ULONG>(bytes_to_send);

        // Правильная 7-аргументная сигнатура WSASend
        if (WSASend(
            clientPtr->_mainSocket,
            &op->buffer,
            1,
            nullptr,
            0,
            &op->overlapped,
            nullptr) == SOCKET_ERROR)
        {
            int error = WSAGetLastError();
            if (error != WSA_IO_PENDING) {
                ReturnOperation(op);
                return -1;
            }
        }
        return 0;
    }

    //
    // ======= Worker_Thread =======
    //

    int Worker_Thread() {
        while (!shutdown_requested_.load()) {
            DWORD bytesTransferred = 0;
            ULONG_PTR completionKey = 0;
            LPOVERLAPPED overlapped = nullptr;

            BOOL success = GetQueuedCompletionStatus(
                iocp_port,
                &bytesTransferred,
                &completionKey,
                &overlapped,
                1000);

            if (shutdown_requested_.load())
                break;

            if (!success && overlapped == nullptr) {
                // Таймаут — нет операций
                continue;
            }

            IOCP_Operation* op = reinterpret_cast<IOCP_Operation*>(overlapped);
            std::shared_ptr<IOCP_Client> basePtr = (op ? op->client : nullptr);
            std::shared_ptr<ClientType> clientPtr = nullptr;

            if (basePtr) {
                clientPtr = std::static_pointer_cast<ClientType>(basePtr);
            }

            if (!clientPtr) {
                Log("WORKER", "ERROR", "Null client pointer", "0.0.0.0", -1);
                if (op) ReturnOperation(op);
                continue;
            }

            if (!success || bytesTransferred == 0) {
                std::string reason = !success
                    ? ("WSA Error: " + std::to_string(WSAGetLastError()))
                    : "Graceful disconnect";

                Log("DISCONNECT", "WARN", "Client disconnected: " + reason,
                    clientPtr->_ip,
                    static_cast<int>(clientPtr->_mainSocket));

                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    if (!clientPtr->is_disconnected) {
                        clientPtr->is_disconnected = true;
                        clients_.erase(clientPtr->_mainSocket);
                    }
                }

                if (op) ReturnOperation(op);
                continue;
            }

            try {
                if (op->type == OperationType::RECV) {
                    //Log("RECV", "SUCCESS", "Received data",
                    //    clientPtr->_ip,
                    //    static_cast<int>(clientPtr->_mainSocket));

                    {
                        std::lock_guard<std::mutex> lock(clients_mutex_);
                        clientPtr->recv_buffer.insert(
                            clientPtr->recv_buffer.end(),
                            op->data,
                            op->data + bytesTransferred);
                        clientPtr->last_active_time = std::chrono::steady_clock::now();
                    }

                    // Вызываем виртуальный метод
                    SERVER_PROCEDURE(clientPtr);
                }
                else if (op->type == OperationType::SEND) {
                    //Log("SEND", "SUCCESS", "Sent data",
                        //clientPtr->_ip,
                        //static_cast<int>(clientPtr->_mainSocket));
                }
            }
            catch (const std::exception& ex) {
                Log("WORKER", "ERROR",
                    std::string("Exception: ") + ex.what(),
                    clientPtr->_ip,
                    static_cast<int>(clientPtr->_mainSocket));

                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    if (!clientPtr->is_disconnected) {
                        clientPtr->is_disconnected = true;
                        clients_.erase(clientPtr->_mainSocket);
                    }
                }
            }

            ReturnOperation(op);
        }

        Log("WORKER", "INFO", "Worker thread exiting gracefully", "0.0.0.0", -1);
        return 0;
    }

    //
    // ======= TimeoutCheckerThread =======
    //

    void TimeoutCheckerThread() {
        const std::chrono::seconds timeout_duration(300);
        const std::chrono::seconds check_interval(10);

        while (timeout_thread_running_.load()) {
            std::this_thread::sleep_for(check_interval);
            auto now = std::chrono::steady_clock::now();

            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (auto it = clients_.begin(); it != clients_.end(); ) {
                auto clientPtr = it->second;
                if (now - clientPtr->last_active_time > timeout_duration) {
                    Log("TIMEOUT", "WARN", "Client timed out", clientPtr->_ip,
                        static_cast<int>(clientPtr->_mainSocket));
                    SafeCloseSocket(clientPtr->_mainSocket);
                    clientPtr->is_disconnected = true;
                    it = clients_.erase(it);
                }
                else {
                    ++it;
                }
            }
        }
    }

    //
    // ======= Виртуальный метод обработки полученных данных =======
    //

    // Переопределять в наследнике
    virtual int SERVER_PROCEDURE(const std::shared_ptr<ClientType>& clientPtr) = 0;

public:
    //
    // ======= Вспомогательные функции =======
    //

    void Log(const std::string& type,
        const std::string& result,
        const std::string& message,
        const std::string& ip,
        int client_id)
    {
        auto Safe = [](const std::string& str) -> std::string {
            return str.empty() ? " " : str;
        };

        IOCP_Log lg;

        SYSTEMTIME st;
        GetLocalTime(&st);

        char buf[32];
        sprintf_s(buf, "%02d:%02d:%02d.%03d",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        lg.time = buf;

        // Применяем безопасную обёртку
        lg.type = Safe(type);
        lg.result = Safe(result);
        lg.message = Safe(message);
        lg.ip = Safe(ip);
        lg.client_id = client_id;

        constexpr size_t max_logs = 10000;
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.push_back(std::move(lg));
        if (logs_.size() > max_logs) {
            logs_.erase(logs_.begin());
        }
    }

protected:
    void SafeCloseSocket(SOCKET& s) {
        if (s != INVALID_SOCKET) {
            shutdown(s, SD_BOTH);
            closesocket(s);
            s = INVALID_SOCKET;
        }
    }

protected:
    unsigned int _port;
    HANDLE iocp_port = nullptr;
};


