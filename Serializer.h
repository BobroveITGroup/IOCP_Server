#pragma once

#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <cstdint>


#define WIN32_LEAN_AND_MEAN             // Исключите редко используемые компоненты из заголовков Windows

class Serializer {
public:
    // Сериализация целых чисел (Big-endian)
    void SerializeInt(int value, std::vector<uint8_t>& buffer) {
        uint32_t beValue = htonl(static_cast<uint32_t>(value));
        buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&beValue),
            reinterpret_cast<uint8_t*>(&beValue) + sizeof(beValue));
    }

    // Десериализация целых чисел (Big-endian)
    int DeserializeInt(const std::vector<uint8_t>& data, size_t& offset) {
        if (offset + sizeof(uint32_t) > data.size()) {
            throw std::runtime_error("Ошибка десериализации: недостаточно данных для целого числа");
        }
        uint32_t beValue;
        std::memcpy(&beValue, data.data() + offset, sizeof(beValue));
        offset += sizeof(beValue);
        return static_cast<int>(ntohl(beValue));
    }

    // Сериализация строки UTF-8
    void SerializeString(const std::string& text, std::vector<uint8_t>& buffer) {
        uint32_t length = static_cast<uint32_t>(text.size());  // Размер строки в байтах UTF-8
        SerializeInt(length, buffer);
        buffer.insert(buffer.end(), text.begin(), text.end());
    }

    // Десериализация строки UTF-8
    std::string DeserializeString(const std::vector<uint8_t>& data, size_t& offset) {
        uint32_t length = DeserializeInt(data, offset);
        if (offset + length > data.size()) {
            throw std::runtime_error("Ошибка десериализации: недостаточно данных для строки");
        }
        std::string text(data.begin() + offset, data.begin() + offset + length);  // Строка UTF-8
        offset += length;
        return text;
    }

    // Сериализация изображения (читаем из файла)
    void SerializeImage(const std::string& imageName, const std::string& directory, std::vector<uint8_t>& buffer) {
        std::string imagePath = directory + "\\" + imageName;
        std::ifstream imageFile(imagePath, std::ios::binary);
        if (!imageFile) {
            std::cout << u8"Ошибка: не удалось открыть файл " << imagePath << std::endl;
        }

        imageFile.seekg(0, std::ios::end);
        size_t imageSize = static_cast<size_t>(imageFile.tellg());
        imageFile.seekg(0, std::ios::beg);

        if (imageSize == 0) {
            std::cout << u8"Ошибка: файл " << imagePath + u8" пуст." << std::endl;
        }

        std::vector<uint8_t> imageData(imageSize);
        imageFile.read(reinterpret_cast<char*>(imageData.data()), imageSize);
        if (imageFile.fail()) {
            std::cout << u8"Ошибка: не удалось прочитать данные из файла " << imagePath << std::endl;
        }
        imageFile.close();

        SerializeString(imageName, buffer);
        SerializeInt(static_cast<int>(imageSize), buffer);
        buffer.insert(buffer.end(), imageData.begin(), imageData.end());
    }

    // Десериализация изображения с сохранением в указанную директорию
    std::pair<std::string, std::string> DeserializeImage(const std::vector<uint8_t>& data, size_t& offset, const std::string& saveDir) {
        // 1. Читаем название файла
        std::string fileName = DeserializeString(data, offset);

        // 2. Читаем размер данных изображения
        int imageSize = DeserializeInt(data, offset);

        // 3. Проверяем, что данных достаточно
        if (offset + imageSize > data.size()) {
            throw std::runtime_error("Ошибка десериализации: недостаточно данных для изображения");
        }

        // 4. Копируем бинарные данные изображения
        std::vector<uint8_t> imageData(data.begin() + offset, data.begin() + offset + imageSize);
        offset += imageSize;

        // 5. Сохраняем в файл (если указана директория)
        std::string savedPath;
        if (!saveDir.empty()) {
            // Формируем полный путь
            savedPath = saveDir;
            if (savedPath.back() != '/' && savedPath.back() != '\\') {
                savedPath += '/';  // Добавляем разделитель, если его нет
            }
            savedPath += fileName;

            // Сохраняем данные в файл
            std::ofstream outFile(savedPath, std::ios::binary);
            if (!outFile) {
                throw std::runtime_error("Не удалось создать файл: " + savedPath);
            }
            outFile.write(reinterpret_cast<const char*>(imageData.data()), imageData.size());
            outFile.close();

            // Проверяем, что запись прошла успешно
            if (!outFile.good()) {
                throw std::runtime_error("Ошибка записи в файл: " + savedPath);
            }
        }

        return { fileName, savedPath };  // Возвращаем имя файла и путь сохранения (если был)
    }

    // Сериализация double (Big-endian)
    void SerializeDouble(double value, std::vector<uint8_t>& buffer) {
        uint64_t raw;
        std::memcpy(&raw, &value, sizeof(raw));

        // Преобразуем в big-endian
        uint64_t beValue =
            ((raw & 0x00000000000000FFULL) << 56) |
            ((raw & 0x000000000000FF00ULL) << 40) |
            ((raw & 0x0000000000FF0000ULL) << 24) |
            ((raw & 0x00000000FF000000ULL) << 8) |
            ((raw & 0x000000FF00000000ULL) >> 8) |
            ((raw & 0x0000FF0000000000ULL) >> 24) |
            ((raw & 0x00FF000000000000ULL) >> 40) |
            ((raw & 0xFF00000000000000ULL) >> 56);

        buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&beValue),
            reinterpret_cast<uint8_t*>(&beValue) + sizeof(beValue));
    }

    // Десериализация double (Big-endian)
    double DeserializeDouble(const std::vector<uint8_t>& data, size_t& offset) {
        if (offset + sizeof(uint64_t) > data.size()) {
            throw std::runtime_error("Ошибка десериализации: недостаточно данных для double");
        }

        uint64_t beValue;
        std::memcpy(&beValue, data.data() + offset, sizeof(beValue));
        offset += sizeof(beValue);

        // Преобразуем из big-endian
        uint64_t raw =
            ((beValue & 0x00000000000000FFULL) << 56) |
            ((beValue & 0x000000000000FF00ULL) << 40) |
            ((beValue & 0x0000000000FF0000ULL) << 24) |
            ((beValue & 0x00000000FF000000ULL) << 8) |
            ((beValue & 0x000000FF00000000ULL) >> 8) |
            ((beValue & 0x0000FF0000000000ULL) >> 24) |
            ((beValue & 0x00FF000000000000ULL) >> 40) |
            ((beValue & 0xFF00000000000000ULL) >> 56);

        double value;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }
};