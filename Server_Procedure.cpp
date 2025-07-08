#include "ShoKuda_server.h"

int ShoKuda_server::SERVER_PROCEDURE(const std::shared_ptr<Client>& clientPtr)
{
	#pragma region PING First
	// Обработка пинга — если первый байт буфера равен 1 байту пинга
	if (!clientPtr->recv_buffer.empty() && clientPtr->recv_buffer[0] == 0x02) {
		Log("PING", "RECV", "Ping byte detected = " + std::to_string(clientPtr->recv_buffer[0]), clientPtr->_ip, clientPtr->ID);

		std::vector<uint8_t> pong = { 0x02 };
		AsyncSend(clientPtr, pong);

		// Удаляем первый байт из буфера — сдвигаем окно
		clientPtr->recv_buffer.clear();

		AsyncRecv(clientPtr);
		return 0;
	}
	#pragma endregion

	#pragma region Get_size_t
	if (clientPtr->expect_bytes == 0 && clientPtr->recv_buffer.size() >= sizeof(uint32_t))
	{
		if (clientPtr->recv_buffer.size() >= sizeof(uint32_t)) // безопасная проверка
		{
			size_t offset = 0;
			clientPtr->expect_bytes = ser.DeserializeInt(clientPtr->recv_buffer, offset);

			clientPtr->recv_full.reserve(clientPtr->expect_bytes);

			Log("RECV", "SUCCESS", "Get full package size = " + std::to_string(clientPtr->expect_bytes),
				clientPtr->_ip, clientPtr->ID);

			// Подтверждение, что размер получен (опционально)
			std::vector<uint8_t> confirmation = { 1 };
			AsyncSend(clientPtr, confirmation);

			clientPtr->recv_buffer.clear();
		}
		
		else
		{
			Log("RECV", "WAIT", "Waiting for size bytes",clientPtr->_ip, clientPtr->ID);
		}
		
	}
	#pragma endregion

	#pragma region Get_parts
	if (clientPtr->expect_bytes > 0 && clientPtr->recv_buffer.size() > 4)
	{
		clientPtr->recv_full.insert(clientPtr->recv_full.end(),clientPtr->recv_buffer.begin(),clientPtr->recv_buffer.end());

		Log("RECV", "SUCCESS", "Received chunk [" +std::to_string(clientPtr->recv_buffer.size()) + " bytes]",clientPtr->_ip, clientPtr->ID);



		clientPtr->recv_buffer.clear();
	}
	#pragma endregion

	#pragma region Check_full
	if (clientPtr->expect_bytes > 0 && clientPtr->recv_full.size() >= clientPtr->expect_bytes)
	{
		Log("RECV", "COMPLETE", "Full packet received [" +std::to_string(clientPtr->recv_full.size()) + " / " +std::to_string(clientPtr->expect_bytes) + "]",clientPtr->_ip, clientPtr->ID);

		clientPtr->expect_bytes = 0; // сбрасываем состояние

		// Вызываем процедуру в зависимости от роли клиента + его этапа
		switch (clientPtr->role)
		{
			case Role_Client::Customer:
				switch (clientPtr->stage)
				{
					case Stage_Client::Authorization:
						Client_Authorization(clientPtr);
					break;

					case Stage_Client::Registration:
						Client_Registration(clientPtr);
					break;

					case Stage_Client::Mainwork:
						Client_MainWork(clientPtr);
					break;
				}
			break;

			case Role_Client::Business:
				switch (clientPtr->stage)
				{
				case Stage_Client::Authorization:
					Client_Authorization(clientPtr);
				break;

				case Stage_Client::Registration:
					Client_Registration(clientPtr);
				break;

				case Stage_Client::Mainwork:
					Client_MainWork(clientPtr);
				break;
				}
			break;

			case Role_Client::Curier:
				switch (clientPtr->stage)
				{
				case Stage_Client::Authorization:
					Client_Authorization(clientPtr);
				break;

				case Stage_Client::Registration:
					Client_Registration(clientPtr);
				break;

				case Stage_Client::Mainwork:
					Client_MainWork(clientPtr);
				break;
				}
			break;
		}
	}
	#pragma endregion

	clientPtr->recv_buffer.clear();
	AsyncRecv(clientPtr);

	return 0;
}