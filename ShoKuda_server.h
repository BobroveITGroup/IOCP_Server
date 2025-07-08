#pragma once

#include<memory>
#include <unordered_set>
#include <map>

#include"IOCP_Server.h"
#include"PostgerSQL.h"
#include"Serializer.h"

// ==========================================================
// ==== Как подключать библиотеки ====
// ==========================================================
// == Конфигурация (например, Debug или Release) ? С/C++ ? Общие ? Дополнительные каталоги включаемых файлов (Additional Include Directories)
// == C:\OpenSSL-Win64\include
// == Конфигурация ? Компоновщик (Linker) ? Общие ? Дополнительные каталоги библиотек (Additional Library Directories)
// == C:\OpenSSL-Win64\lib
// == Конфигурация ? Компоновщик (Linker) ? Ввод (Input) ? Дополнительные зависимости (Additional Dependencies)
// == libssl.lib;libcrypto.lib;Ws2_32.lib;

enum Stage_Client { Authorization, Registration, Mainwork };
enum Role_Client  { Customer, Curier, Business };

struct Client : public IOCP_Client
{
	int ID = -1;

	Stage_Client stage = Stage_Client::Authorization;
	Role_Client role = Role_Client::Customer;
};

class ShoKuda_server : public IOCP_Server<Client>
{
private:
	Serializer ser;

public:
	PostgreSQL sql;


public:
	ShoKuda_server() : IOCP_Server<Client>(8080), sql("host=localhost dbname=ShoKuda user=postgres password=123") {}

	int SERVER_PROCEDURE(const std::shared_ptr<Client>& clientPtr);

	void SendDataInChunks(const std::shared_ptr<Client>& client, const std::vector<uint8_t>& data, size_t chunkSize) {
		if (!client || data.empty()) return;

		// 1. Отправка размера (4 байта) в формате Little-Endian
		std::vector<uint8_t> totalSize;
		ser.SerializeInt(data.size(), totalSize);

		AsyncSend(client, totalSize);

		// 2. Отправка чанков
		size_t sentBytes = 0;
		while (sentBytes < data.size()) {
			size_t currentChunkSize = min(chunkSize, data.size() - sentBytes);
			std::vector<uint8_t> chunk(
				data.begin() + sentBytes,
				data.begin() + sentBytes + currentChunkSize
			);
			AsyncSend(client, chunk);
			sentBytes += currentChunkSize;
		}
	}

private:
	int Client_Authorization(const std::shared_ptr<Client>& clientPtr);
	int Client_Registration(const std::shared_ptr<Client>& clientPtr);
	int Client_MainWork(const std::shared_ptr<Client>& clientPtr);

	#pragma region Procedure_Func
	int Send_Place(const std::shared_ptr<Client>& clientPtr);
	int Send_Restaraunt(const std::shared_ptr<Client>& clientPtr, int ID_Restaraunt);
	int Send_MenuItems(const std::shared_ptr<Client>& clientPtr, int ID_Restaraunt);
	int Send_Category(const std::shared_ptr<Client>& clientPtr, int ID_Restaraunt);
	int Send_Comment(const std::shared_ptr<Client>& clientPtr, int ID_Restaraunt);
	int Send_Orders(const std::shared_ptr<Client>& clientPtr, int Client_id);

	int Recv_order_restaraunt(const std::shared_ptr<Client>& clientPtr);
	int Recv_order_post(const std::shared_ptr<Client>& clientPtr);
	int Recv_order_shop(const std::shared_ptr<Client>& clientPtr);

	int Recv_order_feedback(const std::shared_ptr<Client>& clientPtr, int order_id);

	int Recv_order_status_compleation(const std::shared_ptr<Client>& clientPtr, int order_id);
	#pragma endregion
private:

};

