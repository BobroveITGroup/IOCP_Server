#define _CRT_SECURE_NO_WARNINGS
#pragma once

#include <libpq-fe.h>
#include <functional>
#include <vector>
#include <string>
#include <mutex>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <stdexcept>

#include "MAIN.h"

#define FIELD(res, row, col) std::string(PQgetvalue(res, row, col))
#define FIELD_INT(res, row, col) std::stoi(PQgetvalue(res, row, col))
#define FIELD_DBL(res, row, col) std::stod(PQgetvalue(res, row, col))
#define FIELD_BOOL(res, row, col) (std::string(PQgetvalue(res, row, col)) == "t")

constexpr size_t MAX_LOG_SIZE = 1000;

// Пример структуры лога
struct Log {
    std::string timestamp;
    std::string query;
    std::string result;
};

class PostgreSQL {
public:
    // Конструктор — принимает строку подключения
    PostgreSQL(const std::string& conninfo) {
        connect = PQconnectdb(conninfo.c_str());
        if (PQstatus(connect) != CONNECTION_OK) {
            Log("Constructor", std::string("Connection failed: ") + PQerrorMessage(connect));
            PQfinish(connect);
            connect = nullptr;
        }
        else {
            Log("Constructor", "Connection successful");

            // Устанавливаем кодировку клиента в UTF-8
            PGresult* res = PQexec(connect, "SET client_encoding TO 'UTF8';");
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                Log("Constructor", std::string("Failed to set UTF8 encoding: ") + PQerrorMessage(connect));
            }
            else {
                Log("Constructor", "Client encoding set to UTF8");
            }
            PQclear(res);
        }
    }


    // Деструктор — закрывает соединение при уничтожении объекта
    ~PostgreSQL() {
        if (connect) {
            PQfinish(connect);
            Log("Destructor", "Connection closed");
        }
    }

public:
    std::vector<Log> _logs;
    std::mutex _log_mutex;
    /*
    
    */
    template<typename T> std::vector<T> query(const std::string& sql, const std::vector<std::string>& params, std::function<T(PGresult*, int)> rowParser) {
        std::vector<const char*> paramPtrs;
        for (const auto& p : params)
            paramPtrs.push_back(p.c_str());

        PGresult* res = PQexecParams(connect, sql.c_str(), params.size(), nullptr,
            paramPtrs.data(), nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK)
            throw std::runtime_error(PQerrorMessage(connect));

        int rows = PQntuples(res);
        std::vector<T> result;
        result.reserve(rows);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(connect);
            Log("query", error);
            PQclear(res);
            throw std::runtime_error(error);
        }


        for (int i = 0; i < rows; ++i)
            result.push_back(rowParser(res, i));

        PQclear(res);
        return result;
    }
    void execute(const std::string& sql, const std::vector<std::string>& params) {
        std::vector<const char*> paramPtrs;
        for (const auto& p : params)
            paramPtrs.push_back(p.c_str());

        PGresult* res = PQexecParams(connect, sql.c_str(), params.size(), nullptr,
            paramPtrs.data(), nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::string error = PQerrorMessage(connect);
            PQclear(res);
            throw std::runtime_error(error);
        }
        PQclear(res);
    }

    std::string EscapeString(const std::string& input) {
        if (!is_Connection()) {
            Log("EscapeString", "Failed: no connection");
            return "";
        }

        // Получаем экранированную строку с кавычками (например, 'O''Reilly')
        char* escaped = PQescapeLiteral(connect, input.c_str(), input.length());
        if (!escaped) {
            Log("EscapeString", "Failed to escape: " + input);
            return "";
        }

        std::string result(escaped);
        PQfreemem(escaped);  // Освобождаем память, выделенную libpq

        // Убираем внешние кавычки, чтобы обернуть вручную в Add_values
        if (result.length() >= 2 && result.front() == '\'' && result.back() == '\'') {
            result = result.substr(1, result.length() - 2);
        }

        return result;
    }

    // ✅ Парсинг строки во время
    std::tm parseTimestamp(const std::string& time_only) {
        std::time_t t = std::time(nullptr);
        std::tm now;
        localtime_s(&now, &t);  // безопасно получаем локальное время

        std::ostringstream full_ts;
        full_ts << std::put_time(&now, "%Y-%m-%d") << " " << time_only;

        std::tm result = {};
        std::istringstream ss(full_ts.str());
        ss >> std::get_time(&result, "%Y-%m-%d %H:%M:%S");

        return result;
    }



    // ✅ Форматирование времени в строку
    std::string formatTimestamp(const std::tm& tm) {
        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    // ✅ Безопасное вычитание минут (с localtime_s)
    std::tm subtractMinutes(std::tm tm, int minutes) {
        std::time_t tt = std::mktime(&tm);
        tt -= minutes * 60;

        std::tm new_tm{};
#if defined(_MSC_VER)
        localtime_s(&new_tm, &tt); // Безопасная версия на Windows
#else
        new_tm = *std::localtime(&tt); // На других системах можно использовать
#endif
        return new_tm;
    }

    bool is_Connection() {
        if (!connect) {
            Log("Connection check", "Failed: null connection");
            return false;
        }

        if (PQstatus(connect) != CONNECTION_OK) {
            Log("Connection check", std::string("Failed: ") + PQerrorMessage(connect));
            return false;
        }

        return true;
    }

    void Log(std::string query, std::string result) {
        std::lock_guard<std::mutex> lock(_log_mutex);

        // Форматирование времени
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now;
#ifdef _WIN32
        localtime_s(&tm_now, &now_c);
#else
        localtime_r(&now_c, &tm_now);
#endif

        std::ostringstream timestamp_ss;
        timestamp_ss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");

        // Добавляем лог
        _logs.push_back({
            timestamp_ss.str(),
            std::move(query),
            std::move(result)
            });

        // Удаляем самый старый лог, если превышен лимит
        if (_logs.size() > MAX_LOG_SIZE) {
            _logs.erase(_logs.begin()); // удаляет первый (самый старый)
        }
    }
private:
    PGconn* connect;
};