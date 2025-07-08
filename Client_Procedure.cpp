#include "ShoKuda_server.h"
#include <D:\Work_Misha\http_lib\httplib.h>
#include <D:\Work_Misha\http_lib\json.hpp>
#include <openssl/hmac.h>
#include "CURL.foo.h"



enum Operation {
	SEND_Places = 0,
	SEND_Restaraunts = 1,
	SEND_MenuItems = 3,
	SEND_Category = 2,
	SEND_COMMENT = 4,
	SEND_CUSTOMER_ORDERS = 6,

	RECV_ORDER_RESTARAUNT = 5,
	RECV_ORDER_POST = 120,
	RECV_ORDER_SHOP = 123,
	RECV_ORDER_RESTARAUNT_STATUS = 1232,
	RECV_ORDER_CURIER_FEEDBACK = 1230
};
// Функция для вычисления расстояния (в км) между двумя координатами (используя Haversine formula)
double haversineDistance(double lat1, double lon1, double lat2, double lon2) {
	constexpr double R = 6371.0; // Радиус Земли в км
	double dLat = (lat2 - lat1) * 3.14159265358979323846 / 180.0;
	double dLon = (lon2 - lon1) * 3.14159265358979323846 / 180.0;

	double a = sin(dLat / 2) * sin(dLat / 2) +
		cos(lat1 * 3.14159265358979323846 / 180.0) * cos(lat2 * 3.14159265358979323846 / 180.0) *
		sin(dLon / 2) * sin(dLon / 2);

	double c = 2 * atan2(sqrt(a), sqrt(1 - a));
	return R * c;
}

std::string GenerateHMAC_SHA1(const std::string& key, const std::string& data) {
	unsigned char* digest;
	unsigned int len = 20;

	digest = HMAC(EVP_sha1(),
		key.data(), static_cast<int>(key.length()),
		reinterpret_cast<const unsigned char*>(data.data()), data.length(),
		nullptr, nullptr);

	std::ostringstream result;
	for (unsigned int i = 0; i < len; i++) {
		result << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
	}
	return result.str();
}
std::string base64_encode(const std::string& in) {
	static const std::string base64_chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	std::string out;
	int val = 0, valb = -6;
	for (uint8_t c : in) {
		val = (val << 8) + c;
		valb += 8;
		while (valb >= 0) {
			out.push_back(base64_chars[(val >> valb) & 0x3F]);
			valb -= 6;
		}
	}
	if (valb > -6)
		out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
	while (out.size() % 4)
		out.push_back('=');
	return out;
}


int ShoKuda_server::Client_MainWork(const std::shared_ptr<Client>& clientPtr)
{
	size_t offset = 0;

	int _operationCode = ser.DeserializeInt(clientPtr->recv_full, offset);
	int _datailCode    = ser.DeserializeInt(clientPtr->recv_full, offset);

	switch (_operationCode)
	{
		case SEND_Places:
		{
			Send_Place(clientPtr);
		}
		break;

		case SEND_Restaraunts:
		{
			Send_Restaraunt(clientPtr, _datailCode);
		}
		break;

		case SEND_MenuItems:
		{
			Send_MenuItems(clientPtr, _datailCode);
		}
		break;

		case SEND_COMMENT:
		{
			Send_Comment(clientPtr, _datailCode);
		}
		break;

		case SEND_Category:
		{
			Send_Category(clientPtr, _datailCode);
		}
		break;

		case RECV_ORDER_RESTARAUNT:
		{
			Recv_order_restaraunt(clientPtr);
		}
		break;

		case RECV_ORDER_POST:
		{
			Recv_order_post(clientPtr);
		}
		break;

		case RECV_ORDER_SHOP:
		{
			Recv_order_shop(clientPtr);
		}
		break;

		case SEND_CUSTOMER_ORDERS:
		{
			Send_Orders(clientPtr, _datailCode);
		}
		break;

		case RECV_ORDER_RESTARAUNT_STATUS:
		{
			Recv_order_status_compleation(clientPtr, _datailCode);
		}
		break;

		case RECV_ORDER_CURIER_FEEDBACK:
		{
			Recv_order_feedback(clientPtr, _datailCode);
		}
		break;

		default:
		{

		}
		break;
	}

	clientPtr->recv_full.clear();
	return 0;
}
int ShoKuda_server::Client_Authorization(const std::shared_ptr<Client>& clientPtr)
{
	Log("PROCEDURE", "WARNING", "Start authorization", clientPtr->_ip, clientPtr->ID);

	try
	{
		size_t offset = 0;
		int type = ser.DeserializeInt(clientPtr->recv_full, offset);
		std::string deviceID = ser.DeserializeString(clientPtr->recv_full, offset);
		std::string google_apple_id = ser.DeserializeString(clientPtr->recv_full, offset);

		clientPtr->role = Role_Client(type);

		if (clientPtr->role >= -1 && clientPtr->role <= 3)
		{
			Log("PROCEDURE", "SUCCESS", "CLIENT ROLE : " + std::to_string(int(clientPtr->role)), clientPtr->_ip, clientPtr->ID);
		}
		else
		{
			Log("PROCEDURE", "ERROR", "UNKNOWN CLIENT ROLE " + std::to_string(int(clientPtr->role)), clientPtr->_ip, clientPtr->ID);
			return -1;
		}

		Log("PROCEDURE", "INFO", "DeviceID: " + deviceID + ", AccountID: " + google_apple_id, clientPtr->_ip, clientPtr->ID);

		// Поиск пользователя
		auto rows = sql.query<User>(
			"SELECT id, nickname, image_name FROM users WHERE device_id = $1 OR google_apple_id = $2",
			{ deviceID, google_apple_id },
			[](PGresult* res, int i) {
				return User{
					FIELD_INT(res, i, 0),
					FIELD(res, i, 1),
					FIELD(res, i, 2)
				};
			}
		);

		std::vector<uint8_t> request;

		if (!rows.empty()) {
			// Пользователь найден
			int user_id = rows[0].id;
			Log("AUTH", "SUCCESS", "Client exists. ID = " + std::to_string(user_id), clientPtr->_ip, clientPtr->ID);

			clientPtr->ID = user_id;
			clientPtr->stage = Stage_Client::Mainwork;

			ser.SerializeInt(user_id, request);
			ser.SerializeString(rows[0].nickname, request);
			ser.SerializeImage(rows[0].imagename, "D:\\Work_Misha\\Programing\\Server\\Server\\User_Avatar", request);

			SendDataInChunks(clientPtr, request, 500);
			clientPtr->recv_full.clear();
			return 0;
		}

		// Нет такого пользователя — отправляем сигнал на регистрацию
		clientPtr->stage = Stage_Client::Registration;
		ser.SerializeInt(-14, request);
		SendDataInChunks(clientPtr, request, 500);
		clientPtr->recv_full.clear();
		return 0;
	}
	catch (const std::exception& ex)
	{
		Log("AUTH", "FAILED", "Could not authorize client: " + std::string(ex.what()), clientPtr->_ip, clientPtr->ID);
		return -1;
	}
}
int ShoKuda_server::Client_Registration(const std::shared_ptr<Client>& clientPtr)
{
	Log("PROCEDURE", "INFO", "Start registration", clientPtr->_ip, clientPtr->ID);

	try
	{
		size_t offset = 0;
		auto& data = clientPtr->recv_full;

		auto readStr = [&](const std::string& fieldName) -> std::string {
			std::string value = ser.DeserializeString(data, offset);
			Log("REGISTRATION", "DATA", fieldName + " = " + value, clientPtr->_ip, clientPtr->ID);
			return value;
		};

		Log("REGISTRATION", "INFO", "Deserialization started", clientPtr->_ip, clientPtr->ID);

		std::string deviceID = readStr("deviceID");
		std::string surname = readStr("surname");
		std::string name = readStr("name");
		std::string telephone = readStr("telephone");
		std::string nick = readStr("nickname");

		// Сохраняем временное изображение
		Log("REGISTRATION", "INFO", "Deserializing image", clientPtr->_ip, clientPtr->ID);
		const std::string imageDir = "D:\\Work_Misha\\Programing\\Server\\Server\\User_Avatar";
		const std::string tempImagePath = imageDir + "\\temp.jpg";
		ser.DeserializeImage(data, offset, tempImagePath);
		Log("REGISTRATION", "DATA", "Image saved temporarily", clientPtr->_ip, clientPtr->ID);

		// Экранируем строки для безопасной вставки
		deviceID = sql.EscapeString(deviceID);
		surname = sql.EscapeString(surname);
		name = sql.EscapeString(name);
		telephone = sql.EscapeString(telephone);
		nick = sql.EscapeString(nick);

		Log("REGISTRATION", "INFO", "Inserting data to database", clientPtr->_ip, clientPtr->ID);
		std::string sql_query = "INSERT INTO users_client (id_device, google_apple_id, middlename, name, surname, nickname, telephonenumber, imagename) VALUES ("
			"'" + deviceID + "', "
			"'next update', "
			"'non', "
			"'" + name + "', "
			"'" + surname + "', "
			"'" + nick + "', "
			"'" + telephone + "', "
			"'temp.jpg');"; // временное имя
		sql.execute(sql_query, {});

		Log("REGISTRATION", "INFO", "Retrieving ID from database", clientPtr->_ip, clientPtr->ID);
		auto rows = sql.query<User>(
			"SELECT id, nickname FROM users_client WHERE id_device = $1",
			{ deviceID },
			[](PGresult* res, int i) {
				return User{
					FIELD_INT(res, i, 0),
					FIELD(res, i, 1),
					"" // imagename пока не нужен
				};
			}
		);

		if (rows.empty()) {
			Log("REGISTRATION", "ERROR", "Client not found after insert. deviceID = " + deviceID, clientPtr->_ip, clientPtr->ID);
			std::vector<uint8_t> response;
			ser.SerializeInt(-1, response);
			AsyncSend(clientPtr, response);
			return -1;
		}

		// Переименование изображения
		int newID = rows[0].id;
		std::string finalImageName = nick + "_" + std::to_string(newID) + ".jpg";
		std::string finalImagePath = imageDir + "\\" + finalImageName;

		// Переименовываем файл
		std::rename(tempImagePath.c_str(), finalImagePath.c_str());
		Log("REGISTRATION", "DATA", "Image renamed to: " + finalImageName, clientPtr->_ip, clientPtr->ID);

		// Обновляем имя изображения в базе
		sql.execute("UPDATE users_client SET imagename = $1 WHERE id = $2", { finalImageName, std::to_string(newID) });

		clientPtr->ID = newID;
		clientPtr->stage = Stage_Client::Mainwork;

		Log("REGISTRATION", "SUCCESS", "Client registered successfully with ID = " + std::to_string(clientPtr->ID), clientPtr->_ip, clientPtr->ID);
		clientPtr->recv_full.clear();
		return 0;
	}
	catch (const std::exception& ex)
	{
		Log("REGISTRATION", "ERROR", std::string("Exception: ") + ex.what(), clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> response;
		ser.SerializeInt(-1, response);
		AsyncSend(clientPtr, response);
		return -1;
	}
	catch (...)
	{
		Log("REGISTRATION", "ERROR", "Unknown exception occurred", clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> response;
		ser.SerializeInt(-1, response);
		AsyncSend(clientPtr, response);
		return -1;
	}
}

#pragma region SEND
int ShoKuda_server::Send_Place(const std::shared_ptr<Client>& clientPtr)
{
	Log("PLACE", "INFO", "Start sending place list", clientPtr->_ip, clientPtr->ID);

	try
	{
		struct Place {
			int id;
			double latitude;
			double longitude;
			std::string type_name;
			std::string title;
			std::string subtitle;
			std::string image;
			int place_info_id;
			int restaraunt_id;
		};

		// Получаем все поля из places + имя типа
		auto places = sql.query<Place>(
			R"(SELECT 
					p.id, 
					p.latitude, 
					p.longitude, 
					pt.name, 
					p.title, 
					p.subtitle,
					p.image,
					0
				FROM places p
				JOIN pin_types pt ON p.type = pt.id)",
			{},
			[](PGresult* res, int i) -> Place {
				return Place{
					FIELD_INT(res, i, 0),
					FIELD_DBL(res, i, 1),
					FIELD_DBL(res, i, 2),
					FIELD(res, i, 3),     // type_name
					FIELD(res, i, 4),     // title
					FIELD(res, i, 5),     // subtitle
					FIELD(res, i, 6),     // image
					0,                   // place_info_id (зарезервировано)
					0                    // restaraunt_id
				};
			}
		);

		// Получаем связи "place_id → restaraunt_id"
		auto place_rest_map = sql.query<std::pair<int, int>>(
			"SELECT place_id, restaraunt_id FROM restaraunt_pin",
			{},
			[](PGresult* res, int i) -> std::pair<int, int> {
				return {
					FIELD_INT(res, i, 0),
					FIELD_INT(res, i, 1)
				};
			}
		);

		std::unordered_map<int, int> place_to_rest;
		for (size_t i = 0; i < place_rest_map.size(); ++i) {
			const std::pair<int, int>& pair = place_rest_map[i];
			place_to_rest[pair.first] = pair.second;
		}

		// Присваиваем restaraunt_id каждому месту
		for (size_t i = 0; i < places.size(); ++i) {
			std::unordered_map<int, int>::const_iterator it = place_to_rest.find(places[i].id);
			if (it != place_to_rest.end()) {
				places[i].restaraunt_id = it->second;
			}
		}

		// Отправка
		std::vector<uint8_t> response;

		ser.SerializeInt(places.size(), response);
		SendDataInChunks(clientPtr, response, 500);
		response.clear();

		for (size_t i = 0; i < places.size(); ++i)
		{
			const Place& p = places[i];

			ser.SerializeInt(p.id, response);
			ser.SerializeDouble(p.latitude, response);
			ser.SerializeDouble(p.longitude, response);
			ser.SerializeString(p.type_name, response);
			ser.SerializeString(p.title, response);
			ser.SerializeString(p.subtitle, response);
			ser.SerializeImage(p.image,"D:\\Work_Misha\\Programing\\ShoKuda\\Pin_logo", response);
			ser.SerializeInt(p.place_info_id, response);
			ser.SerializeInt(p.restaraunt_id, response);

			Log("PLACE", "DATA",
				"ID: " + std::to_string(p.id) +
				" TYPE: " + p.type_name +
				" TITLE: " + p.title +
				" REST_ID: " + std::to_string(p.restaraunt_id),
				clientPtr->_ip, clientPtr->ID);

			SendDataInChunks(clientPtr, response, 500);
			response.clear();
		}

		Log("PLACE", "SUCCESS", "Places sent successfully. Count = " + std::to_string(places.size()), clientPtr->_ip, clientPtr->ID);
		return 0;
	}
	catch (const std::exception& ex)
	{
		Log("PLACE", "ERROR", "Exception: " + std::string(ex.what()), clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		AsyncSend(clientPtr, errorResponse);
		return -1;
	}
	catch (...)
	{
		Log("PLACE", "ERROR", "Unknown error", clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		AsyncSend(clientPtr, errorResponse);
		return -1;
	}
}
int ShoKuda_server::Send_Restaraunt(const std::shared_ptr<Client>& clientPtr, int ID_Restaraunt)
{
	Log("RESTAURANT", "INFO", "Start sending restaurant info", clientPtr->_ip, clientPtr->ID);

	try
	{
		struct Schedule {
			int day_of_week;
			std::string open_time;
			std::string close_time;
		};

		struct Restaraunt {
			int id;
			std::string description;
			std::string telephone;
			double rating;
			int rating_count;
			std::vector<std::string> image_filenames;
			std::vector<Schedule> schedule;
		};

		// Получаем основную информацию
		auto results = sql.query<Restaraunt>(
			"SELECT id, description, COALESCE(telephone, ''), rating, rating_count "
			"FROM restaraunt_info WHERE id = $1",
			{ std::to_string(ID_Restaraunt) },
			[](PGresult* res, int i) -> Restaraunt {
				return Restaraunt{
					FIELD_INT(res, i, 0),
					FIELD(res, i, 1),
					FIELD(res, i, 2),
					FIELD_DBL(res, i, 3),
					FIELD_INT(res, i, 4),
					{},
					{}
				};
			}
		);

		if (results.empty()) {
			Log("RESTAURANT", "WARN", "No such restaurant ID: " + std::to_string(ID_Restaraunt), clientPtr->_ip, clientPtr->ID);
			std::vector<uint8_t> err;
			ser.SerializeInt(0, err);
			AsyncSend(clientPtr, err);
			return -1;
		}

		auto& rest = results[0];

		// Получаем изображения (пути к файлам)
		auto image_files = sql.query<std::string>(
			"SELECT image_url FROM restaraunt_image ri "
			"JOIN restaraunt_image_pair rip ON rip.image_id = ri.id "
			"WHERE rip.restaraunt_id = $1 ORDER BY image_id",
			{ std::to_string(ID_Restaraunt) },
			[](PGresult* res, int i) -> std::string {
				return FIELD(res, i, 0);
			}
		);
		rest.image_filenames = std::move(image_files);

		// Получаем расписание
		auto schedules = sql.query<Schedule>(
			"SELECT weekday_id, open_time, close_time FROM working_hours WHERE restaraunt_id = $1 ORDER BY weekday_id",
			{ std::to_string(ID_Restaraunt) },
			[](PGresult* res, int i) -> Schedule {
				return Schedule{
					FIELD_INT(res, i, 0),
					FIELD(res, i, 1),
					FIELD(res, i, 2)
				};
			}
		);
		rest.schedule = std::move(schedules);

		// Начинаем сериализацию
		std::vector<uint8_t> response;

		// 1. ID
		ser.SerializeInt(rest.id, response);

		// 2. Кол-во изображений
		ser.SerializeInt((int)rest.image_filenames.size(), response);

		for (size_t i = 0; i < rest.image_filenames.size(); ++i) {
			const std::string& filename = rest.image_filenames[i];
			std::string fullPath = "D:\\Work_Misha\\Programing\\ShoKuda\\Restaraunt_images\\" + filename;

			std::ifstream file(fullPath, std::ios::binary);
			if (!file) {
				Log("RESTAURANT", "WARN", "Image file not found: " + fullPath, clientPtr->_ip, clientPtr->ID);
				std::cout << fullPath << std::endl;
				ser.SerializeString("", response);
				ser.SerializeInt(0, response);
				continue;
			}

			std::vector<uint8_t> imageData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			ser.SerializeImage(filename,"D:\\Work_Misha\\Programing\\ShoKuda\\Restaraunt_images", response);
		}

		// 3. Кол-во дней расписания
		auto parseTimeHM = [](const std::string& timeStr, int& hour, int& minute) {
			hour = 0; minute = 0;
			sscanf_s(timeStr.c_str(), "%d:%d", &hour, &minute);
		};

		ser.SerializeInt((int)rest.schedule.size(), response);

		for (const auto& s : rest.schedule) {
			ser.SerializeInt(s.day_of_week, response);

			int open_h, open_m;
			int close_h, close_m;

			parseTimeHM(s.open_time, open_h, open_m);
			parseTimeHM(s.close_time, close_h, close_m);

			ser.SerializeInt(open_h, response);
			ser.SerializeInt(open_m, response);

			ser.SerializeInt(close_h, response);
			ser.SerializeInt(close_m, response);
		}

		// 4. Описание
		ser.SerializeString(rest.description, response);

		// 5. Телефон
		ser.SerializeString(rest.telephone, response);

		// 6. Рейтинг
		ser.SerializeDouble(rest.rating, response);

		// 7. Кол-во оценок
		ser.SerializeInt(rest.rating_count, response);

		Log("RESTAURANT", "DATA",
			"ID: " + std::to_string(rest.id) +
			" IMG: " + std::to_string(rest.image_filenames.size()) +
			" SCHEDULE: " + std::to_string(rest.schedule.size()) +
			" TEL: " + rest.telephone,
			clientPtr->_ip, clientPtr->ID);

		SendDataInChunks(clientPtr, response, 500);
		return 0;
	}
	catch (const std::exception& ex)
	{
		Log("RESTAURANT", "ERROR", "Exception: " + std::string(ex.what()), clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		AsyncSend(clientPtr, errorResponse);
		return -1;
	}
	catch (...)
	{
		Log("RESTAURANT", "ERROR", "Unknown error", clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		AsyncSend(clientPtr, errorResponse);
		return -1;
	}
}
int ShoKuda_server::Send_MenuItems(const std::shared_ptr<Client>& clientPtr, int restaraunt_id)
{
	Log("MENU_ITEM", "INFO", "Start sending menu items for restaraunt_id = " + std::to_string(restaraunt_id), clientPtr->_ip, clientPtr->ID);

	try
	{
		struct MenuItem {
			int id;
			std::string name;
			std::string description;
			double price;
			int weight;
			int category_id;
			int cooking_time;
			bool is_age_control;
			bool is_fast_applimet;
			std::string image;
			int stock;
		};

		auto items = sql.query<MenuItem>(
			"SELECT mi.id, mi.name, mi.description, mi.price, mi.weight, "
			"       mi.category_id, mi.cooking_time, "
			"       mi.is_age_control, mi.is_fast_applimet, "
			"       COALESCE(mi.image, ''), COALESCE(mi.stock, 0) "
			"FROM menu_item mi "
			"JOIN restaraunt_menu_item rmi ON mi.id = rmi.menu_item_id "
			"WHERE rmi.restaraunt_id = $1 "
			"ORDER BY mi.id",
			{ std::to_string(restaraunt_id) },
			[](PGresult* res, int i) -> MenuItem {
				return MenuItem{
					FIELD_INT(res, i, 0),             // id
					FIELD(res, i, 1),                 // name
					FIELD(res, i, 2),                 // description
					FIELD_DBL(res, i, 3),             // price
					FIELD_INT(res, i, 4),             // weight
					FIELD_INT(res, i, 5),             // category_id
					FIELD_INT(res, i, 6),             // cooking_time
					FIELD_BOOL(res, i, 7),			  // is_age_control
					FIELD_BOOL(res, i, 8),			  // is_fast_applimet
					FIELD(res, i, 9),                 // image
					FIELD_INT(res, i, 10)             // stock
				};
			}
		);

		std::vector<uint8_t> response;
		response.reserve(15000);

		ser.SerializeInt(items.size(), response);
		SendDataInChunks(clientPtr, response, 500);
		response.clear();

		for (const auto& item : items)
		{
			ser.SerializeInt(item.id, response);                          // [1] id
			ser.SerializeString(item.name, response);                     // [2] name
			ser.SerializeString(item.description, response);              // [3] description
			ser.SerializeDouble(item.price, response);                    // [4] price
			ser.SerializeInt(item.weight, response);                      // [5] weight
			ser.SerializeInt(item.category_id, response);                 // [6] category_id
			ser.SerializeInt(item.cooking_time, response);                // [7] cooking_time
			ser.SerializeInt(item.is_age_control ? 1 : 0, response);      // [8] bool
			ser.SerializeInt(item.is_fast_applimet ? 1 : 0, response);	  // [9] bool
			ser.SerializeImage(item.image, "D:\\Work_Misha\\Programing\\ShoKuda\\Restaraunt_menu", response);                // [10] image
			ser.SerializeInt(item.stock, response);                       // [11] stock


			Log("MENU_ITEM", "DATA", "ID: " + std::to_string(item.id) + " NAME: " + item.name, clientPtr->_ip, clientPtr->ID);
			SendDataInChunks(clientPtr, response, 500);
			response.clear();
		}

		Log("MENU_ITEM", "SUCCESS", "Menu items sent successfully. Count = " + std::to_string(items.size()), clientPtr->_ip, clientPtr->ID);
		return 0;
	}
	catch (const std::exception& ex)
	{
		Log("MENU_ITEM", "ERROR", "Exception: " + std::string(ex.what()), clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		AsyncSend(clientPtr, errorResponse);
		return -1;
	}
	catch (...)
	{
		Log("MENU_ITEM", "ERROR", "Unknown error", clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		AsyncSend(clientPtr, errorResponse);
		return -1;
	}
}


int ShoKuda_server::Send_Category(const std::shared_ptr<Client>& clientPtr, int ID_Restaraunt)
{
	Log("CATEGORY", "INFO", "Start sending categories for restaraunt_id = " + std::to_string(ID_Restaraunt), clientPtr->_ip, clientPtr->ID);

	try
	{
		struct Category {
			int id;
			std::string name;
		};

		// Загружаем категории, связанные через restaraunt_category с заданным restaraunt_id
		auto categories = sql.query<Category>(
			"SELECT c.id, c.name "
			"FROM category c "
			"JOIN restaraunt_category rc ON c.id = rc.category_id "
			"WHERE rc.restaraunt_id = $1 "
			"ORDER BY c.id",
			{ std::to_string(ID_Restaraunt) },
			[](PGresult* res, int i) -> Category {
				return Category{
					FIELD_INT(res, i, 0),
					FIELD(res, i, 1)
				};
			}
		);

		std::vector<uint8_t> response;

		// Отправляем количество категорий
		ser.SerializeInt(categories.size(), response);
		SendDataInChunks(clientPtr, response, 500);
		response.clear();

		// Отправляем каждую категорию
		for (const auto& cat : categories)
		{
			ser.SerializeInt(cat.id, response);
			ser.SerializeString(cat.name, response);

			Log("CATEGORY", "DATA", "ID: " + std::to_string(cat.id) + " NAME: " + cat.name, clientPtr->_ip, clientPtr->ID);

			SendDataInChunks(clientPtr, response, 500);
			response.clear();
		}

		Log("CATEGORY", "SUCCESS", "Categories sent successfully. Count = " + std::to_string(categories.size()), clientPtr->_ip, clientPtr->ID);
		return 0;
	}
	catch (const std::exception& ex)
	{
		Log("CATEGORY", "ERROR", "Exception: " + std::string(ex.what()), clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		AsyncSend(clientPtr, errorResponse);
		return -1;
	}
	catch (...)
	{
		Log("CATEGORY", "ERROR", "Unknown error", clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		AsyncSend(clientPtr, errorResponse);
		return -1;
	}
}

int ShoKuda_server::Send_Comment(const std::shared_ptr<Client>& clientPtr, int ID_Restaraunt) {
	Log("COMMENT", "INFO", "Start sending comments for restaurant_id = " + std::to_string(ID_Restaraunt), clientPtr->_ip, clientPtr->ID);

	try {
		struct Comment {
			int id;
			int user_id;
			std::string nickname;
			std::string user_image_name;
			std::string comment_text;
			int comment_rating;
			std::string created_at;
		};

		auto comments = sql.query<Comment>(
			R"(
        SELECT c.id, c.user_id, 
               COALESCE(u.nickname, '') AS nickname, 
               COALESCE(u.image_name, '') AS user_image_name, 
               c.comment_text, c.comment_rating, 
               to_char(c.created_at, 'YYYY-MM-DD HH24:MI:SS')
        FROM comments c
        JOIN restaraunt_comments rc ON rc.comment_id = c.id
        LEFT JOIN users u ON u.id = c.user_id
        WHERE rc.restaraunt_id = $1
        ORDER BY c.created_at DESC
    )",
			{ std::to_string(ID_Restaraunt) },
			[](PGresult* res, int i) -> Comment {
				return Comment{
					FIELD_INT(res, i, 0),        // id
					FIELD_INT(res, i, 1),        // user_id
					FIELD(res, i, 2),            // nickname из users
					FIELD(res, i, 3),            // user_image_name
					FIELD(res, i, 4),            // comment_text
					FIELD_INT(res, i, 5),        // comment_rating
					FIELD(res, i, 6)             // created_at
				};
			}
		);

		std::vector<uint8_t> response;
		ser.SerializeInt((int)comments.size(), response);
		SendDataInChunks(clientPtr, response, 500);
		response.clear();

		for (const auto& cm : comments) {
			ser.SerializeInt(cm.id, response);
			ser.SerializeInt(cm.user_id, response);
			ser.SerializeString(cm.nickname, response);

			// Полный путь к аватару пользователя, если есть
			std::string avatarPath;
			if (!cm.user_image_name.empty()) {
				avatarPath = "D:\\Work_Misha\\Programing\\Server\\Server\\User_Avatar\\" + cm.user_image_name;
			}
			else {
				avatarPath.clear();
			}
			ser.SerializeImage(cm.user_image_name, "D:\\Work_Misha\\Programing\\Server\\Server\\User_Avatar", response);

			ser.SerializeString(cm.comment_text, response);
			ser.SerializeInt(cm.comment_rating, response);
			ser.SerializeString(cm.created_at, response);

			Log("COMMENT", "DATA", "ID: " + std::to_string(cm.id) + " USER: " + cm.nickname, clientPtr->_ip, clientPtr->ID);

			SendDataInChunks(clientPtr, response, 500);
			response.clear();
		}

		Log("COMMENT", "SUCCESS", "Comments sent successfully. Count = " + std::to_string(comments.size()), clientPtr->_ip, clientPtr->ID);
		return 0;
	}
	catch (const std::exception& ex) {
		Log("COMMENT", "ERROR", "Exception: " + std::string(ex.what()), clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		AsyncSend(clientPtr, errorResponse);
		return -1;
	}
	catch (...) {
		Log("COMMENT", "ERROR", "Unknown error", clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		AsyncSend(clientPtr, errorResponse);
		return -1;
	}
}

int ShoKuda_server::Send_Orders(const std::shared_ptr<Client>& clientPtr, int Client_id)
{
	Log("ORDER", "INFO", "Start sending orders for client_id = " + std::to_string(Client_id), clientPtr->_ip, clientPtr->ID);

	struct OrderItem {
		int id;
		int menu_item_id;
		std::string title;
		std::string restaurant_name;
		double latitude;
		double longitude;
		double price;
		double weight;
		int is_adult_only;
		int is_quick;
		int quality;
	};

	struct Order {
		int id;
		std::string status;
		double total_price;
		std::string delivery_address;
		std::string delivery_time;
		double latitude;
		double longitude;
		std::string created_at;
		std::vector<OrderItem> items;
	};

	try {
		// 📦 Получение всех заказов клиента
		auto orders = sql.query<Order>(
			"SELECT id, status, total_price, "
			"COALESCE(delivery_address, '') AS delivery_address, "
			"to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') AS delivery_time, "
			"delivery_latitude, delivery_longitude, "
			"to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at "
			"FROM orders "
			"WHERE client_id = $1 AND status != 'success' "
			"ORDER BY created_at DESC",
			{ std::to_string(Client_id) },
			[](PGresult* res, int i) -> Order {
				return Order{
					FIELD_INT(res, i, 0),        // id
					FIELD(res, i, 1),            // status
					std::stod(FIELD(res, i, 2)), // total_price
					FIELD(res, i, 3),            // delivery_address
					FIELD(res, i, 4),            // delivery_time
					std::stod(FIELD(res, i, 5)), // delivery_latitude
					std::stod(FIELD(res, i, 6)), // delivery_longitude
					FIELD(res, i, 7),            // created_at
					{}                           // items
				};
			}
		);

		// 📌 Для каждого заказа загружаем его позиции
		for (auto& order : orders) {
			order.items = sql.query<OrderItem>(
				"SELECT "
				"oi.id, "
				"oi.menu_item_id, "
				"mi.name AS title, "
				"pi.name AS restaurant_name, "
				"p.latitude, p.longitude, "
				"mi.price, mi.weight, "
				"mi.is_age_control, "
				"mi.is_fast_implementation, "
				"oi.quantity "
				"FROM order_items oi "
				"JOIN menu_item mi ON mi.id = oi.menu_item_id "
				"JOIN order_restaurants orr ON orr.id = oi.order_restaurant_id "
				"JOIN place_info pi ON pi.id = orr.restaurant_id "
				"JOIN place p ON p.place_info_id = pi.id "
				"WHERE oi.order_id = $1",
				{ std::to_string(order.id) },
				[](PGresult* res, int i) -> OrderItem {
					return OrderItem{
						FIELD_INT(res, i, 0),			// id
						FIELD_INT(res, i, 1),			// menu_item_id
						FIELD(res, i, 2),				// title
						FIELD(res, i, 3),				// restaurant_name
						std::stod(FIELD(res, i, 4)),	// latitude
						std::stod(FIELD(res, i, 5)),	// longitude
						std::stod(FIELD(res, i, 6)),	// price
						std::stod(FIELD(res, i, 7)),	// weight
						FIELD_INT(res, i, 8),			// is_adult_only
						FIELD_INT(res, i, 9),			// is_quick
						FIELD_INT(res, i, 10)			// quality
					};
				}
			);
		}

		// 🚀 Отправка количества заказов
		std::vector<uint8_t> response;
		ser.SerializeInt(orders.size(), response);
		SendDataInChunks(clientPtr, response, 500);
		response.clear();

		// 🔄 Отправка каждого заказа
		for (const auto& order : orders) {
			ser.SerializeInt(order.id, response);
			ser.SerializeString(order.status, response);
			ser.SerializeDouble(order.total_price, response);
			ser.SerializeString(order.delivery_address, response);
			ser.SerializeString(order.delivery_time, response);
			ser.SerializeDouble(order.latitude, response);
			ser.SerializeDouble(order.longitude, response);
			ser.SerializeString(order.created_at, response);

			ser.SerializeInt(order.items.size(), response);
			for (const auto& item : order.items) {
				ser.SerializeInt(item.id, response);
				ser.SerializeInt(item.menu_item_id, response);
				ser.SerializeString(item.title, response);
				ser.SerializeString(item.restaurant_name, response);
				ser.SerializeDouble(item.latitude, response);
				ser.SerializeDouble(item.longitude, response);
				ser.SerializeDouble(item.price, response);
				ser.SerializeDouble(item.weight, response);
				ser.SerializeInt(item.is_adult_only, response);
				ser.SerializeInt(item.is_quick, response);
				ser.SerializeInt(item.quality, response);
			}

			Log("ORDER", "DATA", "Order ID: " + std::to_string(order.id) + " Items: " + std::to_string(order.items.size()), clientPtr->_ip, clientPtr->ID);
			SendDataInChunks(clientPtr, response, 500);
			response.clear();
		}

		Log("ORDER", "SUCCESS", "Sent enriched orders", clientPtr->_ip, clientPtr->ID);
		return 0;
	}
	catch (const std::exception& ex) {
		Log("ORDER", "ERROR", "Exception: " + std::string(ex.what()), clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		AsyncSend(clientPtr, errorResponse);
		return -1;
	}
	catch (...) {
		Log("ORDER", "ERROR", "Unknown error", clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		AsyncSend(clientPtr, errorResponse);
		return -1;
	}
}
#pragma endregion

#pragma region RECV
int ShoKuda_server::Recv_order_restaraunt(const std::shared_ptr<Client>& clientPtr)
{
	Log("ORDER", "INFO", "Start receiving order", clientPtr->_ip, clientPtr->ID);

	struct Order_item
	{
		int ID;
		int quality;
		int restaurant_id; // Добавляем сюда
	};

	try
	{
	#pragma region Deserialize_Order_Data
			// === Десериализация данных заказа из пакета клиента ===
			size_t offset = 8;
			double latitude = ser.DeserializeDouble(clientPtr->recv_full, offset);
			double longitude = ser.DeserializeDouble(clientPtr->recv_full, offset);
			double delivery_time_unix = ser.DeserializeDouble(clientPtr->recv_full, offset);
			double distance = ser.DeserializeDouble(clientPtr->recv_full, offset);

			int is_age_control_int = ser.DeserializeInt(clientPtr->recv_full, offset);
			bool is_age_control = (is_age_control_int != 0);

			int is_fast_implementation_int = ser.DeserializeInt(clientPtr->recv_full, offset);
			bool is_fast_implementation = (is_fast_implementation_int != 0);

			double delivery_fee = ser.DeserializeDouble(clientPtr->recv_full, offset);
			double items_sum = ser.DeserializeDouble(clientPtr->recv_full, offset);
			double multiple_places_surcharge = ser.DeserializeDouble(clientPtr->recv_full, offset);
			double commission = ser.DeserializeDouble(clientPtr->recv_full, offset);
			double total_sum = ser.DeserializeDouble(clientPtr->recv_full, offset);

			int item_count = ser.DeserializeInt(clientPtr->recv_full, offset);
			std::vector<Order_item>items;
			items.reserve(item_count);
			for (int a = 0; a < item_count; a++)
			{
				int ID = ser.DeserializeInt(clientPtr->recv_full, offset);			// Item ID
				int quality = ser.DeserializeInt(clientPtr->recv_full, offset);		// количество
				int restaurant_id = ser.DeserializeInt(clientPtr->recv_full, offset); // Новое поле из пакета
				items.push_back({ ID,quality,restaurant_id });
			}
	#pragma endregion

			double all_sum_courier = 0.00;      // Итоговая сумма для курьера (можно считать дальше)
			double courier_salary = 0.00;       // Зарплата курьера, зависит от типа доставки
			double company_profit = 0.00;       // Прибыль компании с заказа
			double commission_5 = 0.00;         // Комиссия 5% (если нужна, пока не используется)
			double is_anyplace = 0.00;          // Возможно, доплата за мульти-площадки (если надо)

	#pragma region Courier_TimeAndType
			// === Определение типа курьера, расчет зарплаты и прибыли компании по расстоянию ===
			double busy_from = delivery_time_unix - (55 * 60);
			double busy_to = delivery_time_unix;

			const double center_latitude = 48.4680;
			const double center_longitude = 35.0410;

			double distanceToCenter = haversineDistance(latitude, longitude, center_latitude, center_longitude);

			int courier_type_id = 0;
			if (distanceToCenter <= 1.5) {
				courier_type_id = 1; // Пеший курьер
				courier_salary = 50.00;
				company_profit = 30.00;
			}
			else if (distanceToCenter <= 4.0) {
				courier_type_id = 2; // Автокурьер
				courier_salary = 100.00;
				company_profit = 20.00;
			}
			else {
				courier_type_id = 0; // Курьер недоступен
			}
	#pragma endregion

	#pragma region Find_Free_Courier
		// === Поиск свободного курьера по типу и времени ===
		std::string sql_query = R"sql(
		SELECT c.id 
		FROM couriers c 
		LEFT JOIN courier_schedule cs ON cs.courier_id = c.id 
		AND cs.busy_from < to_timestamp($3) 
		AND cs.busy_to > to_timestamp($2) 
		WHERE c.type_id = $1 
		AND cs.id IS NULL 
		AND c.is_available = true
		ORDER BY c.completed_orders_count ASC
		LIMIT 1
		)sql";

		std::vector<std::string> params = {
			std::to_string(courier_type_id),
			std::to_string(static_cast<int64_t>(busy_from)),
			std::to_string(static_cast<int64_t>(busy_to))
		};

		auto courier_ids = sql.query<int>(sql_query, params, [](PGresult* res, int row) {
			return std::stoi(PQgetvalue(res, row, 0));
			});
		int selected_courier_id = 0;
		if (courier_ids.empty()) {
			// Если нет свободных курьеров — ищем ближайшее время освобождения
			std::string fallback_query = R"sql(
				SELECT MIN(cs.busy_to) 
				FROM couriers c 
				JOIN courier_schedule cs ON cs.courier_id = c.id 
				WHERE c.type_id = $1 AND c.is_available = true
			)sql";

			std::vector<std::string> fallback_params = {
				std::to_string(courier_type_id)
			};

			auto fallback_times = sql.query<double>(fallback_query, fallback_params, [](PGresult* res, int row) {
				char* val = PQgetvalue(res, row, 0);
				return val ? std::stod(val) : 0.0;
				});

			if (!fallback_times.empty() && fallback_times[0] > 0.0) {
				time_t next_available_time = static_cast<time_t>(fallback_times[0]);
				std::tm tm_info;
				localtime_s(&tm_info, &next_available_time);

				char buffer[32];
				std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm_info);

				std::ostringstream msg;
				msg << "Найближчий вільний кур'єр буде доступний о " << buffer;

				std::vector<uint8_t> response;
				ser.SerializeInt(-102, response); // Нет курьеров сейчас, но будет позже
				ser.SerializeString(msg.str(), response);
				SendDataInChunks(clientPtr, response, 500);
			}
			else {
				std::vector<uint8_t> response;
				ser.SerializeInt(-103, response);
				ser.SerializeString("Кур'єри тимчасово недоступні", response);
				SendDataInChunks(clientPtr, response, 500);
			}
			return -1;
		}
		else {
			selected_courier_id = courier_ids[0];
		}
#pragma endregion

	#pragma region Check_Item_Stock
		// === Проверка доступности позиций по количеству ===
		for (const auto& item : items)
		{
			std::string stock_check_query = "SELECT name, stock FROM menu_item WHERE id = $1";
			std::vector<std::string> stock_params = { std::to_string(item.ID) };

			auto stock_results = sql.query<std::pair<std::string, int>>(stock_check_query, stock_params,
				[](PGresult* res, int row) {
					std::string name = PQgetvalue(res, row, 0);
					int stock = std::stoi(PQgetvalue(res, row, 1));
					return std::make_pair(name, stock);
				});

			if (stock_results.empty()) {
				std::vector<uint8_t> response;
				ser.SerializeInt(-104, response);
				ser.SerializeString("Позиція не знайдена: ID " + std::to_string(item.ID), response);
				SendDataInChunks(clientPtr, response, 500);
				return -1;
			}
			else {
				const std::string& name = stock_results[0].first;
				int stock = stock_results[0].second;

				if (stock < item.quality) {
					std::ostringstream msg;
					msg << "⛔ Позиція: *" << name << "*\n"
						<< "• Запрошено: " << item.quality << "\n"
						<< "• Доступно зараз: " << stock;

					std::vector<uint8_t> response;
					ser.SerializeInt(-105, response);
					ser.SerializeString(msg.str(), response);
					SendDataInChunks(clientPtr, response, 500);
					return -1;
				}
			}
		}
#pragma endregion

	#pragma region Insert_Order_And_Items
		// === Вставка заказа, позиций и расписания курьера в БД ===
		sql.execute("BEGIN", {});
		std::string insert_order_sql = R"sql(
		INSERT INTO order_info (
		delivery_latitude, delivery_longitude, delivery_time, 
		total_sum, is_age_control, is_fast_applimet, courier_id
		)
		VALUES (
		$1, $2, to_timestamp($3), $4, $5, $6, $7
		)
		RETURNING id
		)sql";

		std::vector<std::string> insert_order_params = {
			std::to_string(latitude),
			std::to_string(longitude),
			std::to_string(static_cast<int64_t>(delivery_time_unix)),
			std::to_string(total_sum),
			std::to_string(is_age_control ? 1 : 0),
			std::to_string(is_fast_implementation ? 1 : 0),
			std::to_string(selected_courier_id)
		};

		int order_id = sql.query<int>(insert_order_sql, insert_order_params, [](PGresult* res, int row) {
			return std::stoi(PQgetvalue(res, row, 0));
			})[0];

			std::string insert_item_sql = R"sql(
			INSERT INTO order_item (order_id, menu_item_id, quantity, restaurant_id)
			VALUES ($1, $2, $3, $4)
			)sql";

			for (const auto& item : items) {
				std::vector<std::string> insert_item_params = {
					std::to_string(order_id),
					std::to_string(item.ID),
					std::to_string(item.quality),
					std::to_string(item.restaurant_id) // Добавляем сюда
				};
				sql.execute(insert_item_sql, insert_item_params);
			}


			std::string insert_schedule_sql = R"sql(
			INSERT INTO courier_schedule (courier_id, busy_from, busy_to, order_id)
			VALUES ($1, to_timestamp($2), to_timestamp($3), $4)
			)sql";

			std::vector<std::string> insert_schedule_params = {
				std::to_string(selected_courier_id),
				std::to_string(static_cast<int64_t>(busy_from)),
				std::to_string(static_cast<int64_t>(busy_to)),
				std::to_string(order_id)
			};

			sql.execute(insert_schedule_sql, insert_schedule_params);

			sql.execute("COMMIT", {});
	#pragma endregion

	#pragma region Check restaraunt id in order
			// Подсчёт количества уникальных ресторанов в заказе
			std::string count_restaurants_sql = R"sql(SELECT COUNT(DISTINCT restaurant_id) FROM order_item WHERE order_id = $1)sql";

			std::vector<std::string> count_params = { std::to_string(order_id) };

			auto restaurant_count_vec = sql.query<int>(count_restaurants_sql, count_params, [](PGresult* res, int row) {
				return std::stoi(PQgetvalue(res, row, 0));
				});

			int unique_restaurant_count = restaurant_count_vec.empty() ? 1 : restaurant_count_vec[0];

			// Расчёт доплаты за несколько заведений
			double multiple_places_fee = 0.0;
			if (unique_restaurant_count > 1) {
				multiple_places_fee = 45.0 * (unique_restaurant_count - 1);
			}
			else {
				multiple_places_fee = 0.0; // Можно явно, но необязательно
			}

			is_anyplace = (unique_restaurant_count > 1) ? 45.0 * (unique_restaurant_count - 1) : 0.0;

	#pragma endregion

	#pragma region all_sum for ShoKuda
		commission_5 = (courier_salary + company_profit + is_anyplace) * 0.05;
		all_sum_courier = courier_salary + company_profit + commission_5 + is_anyplace;
	#pragma endregion

	#pragma region URLs for restaraunt 
		// === Собираем уникальные ID ресторанов в заказе ===
		std::set<int> unique_restaurant_ids;
		for (size_t i = 0; i < items.size(); ++i) {
			unique_restaurant_ids.insert(items[i].restaurant_id);
		}

		// 0. Получаем email клиента по clientPtr->user_id
		std::string client_email = "";
		if (clientPtr && clientPtr->ID > 0) {
			std::string email_sql = "SELECT email FROM users WHERE id = $1 LIMIT 1";
			std::vector<std::string> email_params = { std::to_string(clientPtr->ID) };

			auto emails = sql.query<std::string>(email_sql, email_params, [](PGresult* res, int row) {
				return std::string(PQgetvalue(res, row, 0));
				});

			if (!emails.empty()) {
				client_email = emails[0];
			}
		}

		for (auto it = unique_restaurant_ids.begin(); it != unique_restaurant_ids.end(); ++it)
		{
			int rest_id = *it;

			// 1. Получаем merchant_secret_key и merchant_account
			std::string merchant_query = R"sql(
        SELECT r.merchant_account, s.merchant_secret_key 
        FROM restaraunt_info r 
        JOIN restaraunt_payment_secret s ON s.restaraunt_id = r.id 
        WHERE r.id = $1 AND r.wayforpay_enabled = true
    )sql";

			std::vector<std::string> merchant_params = { std::to_string(rest_id) };

			auto merchant_info = sql.query<std::pair<std::string, std::string>>(
				merchant_query, merchant_params,
				[](PGresult* res, int row) {
					std::string account = PQgetvalue(res, row, 0);
					std::string secret = PQgetvalue(res, row, 1);
					return std::make_pair(account, secret);
				});

			if (merchant_info.empty()) {
				Log("ORDER", "ERROR", "Merchant account or secret not found for restaurant_id: " + std::to_string(rest_id), clientPtr->_ip, clientPtr->ID);
				continue;
			}

			const std::string& merchant_account = merchant_info[0].first;
			const std::string& secret_key = merchant_info[0].second;

			// 2. Получаем позиции этого ресторана
			std::string item_sql = R"sql(
    SELECT m.name, m.price, oi.quantity, m.tax_code, m.excise_amount, m.product_code 
    FROM order_item oi 
    JOIN menu_item m ON m.id = oi.menu_item_id 
    WHERE oi.order_id = $1 AND oi.restaurant_id = $2
)sql";

			std::vector<std::string> item_params = {
				std::to_string(order_id),
				std::to_string(rest_id)
			};

			auto product_rows = sql.query<std::tuple<std::string, double, int, std::string, double, std::string>>(
				item_sql, item_params,
				[](PGresult* res, int row) {
					std::string name = PQgetvalue(res, row, 0);
					double price = std::stod(PQgetvalue(res, row, 1));
					int qty = std::stoi(PQgetvalue(res, row, 2));
					std::string tax_code = PQgetvalue(res, row, 3);
					double excise = PQgetisnull(res, row, 4) ? 0.0 : std::stod(PQgetvalue(res, row, 4));
					std::string product_code = PQgetvalue(res, row, 5);
					return std::make_tuple(name, price, qty, tax_code, excise, product_code);
				}
			);

			std::vector<std::string> product_names;
			std::vector<double> product_prices;
			std::vector<int> product_counts;
			std::vector<std::string> product_tax_codes;
			std::vector<double> product_excises;
			std::vector<std::string> product_product_codes;

			double sum_for_this_restaurant = 0.0;

			for (const auto& row : product_rows)
			{
				const std::string& name = std::get<0>(row);
				double price = std::get<1>(row);
				int qty = std::get<2>(row);
				const std::string& tax_code = std::get<3>(row);
				double excise = std::get<4>(row);
				const std::string& product_code = std::get<5>(row);

				double price_with_commission = price * 1.05;
				double total = price_with_commission * qty;

				product_names.push_back(name);
				product_prices.push_back(price_with_commission);
				product_counts.push_back(qty);

				sum_for_this_restaurant += total;

				// Добавляем фискальные поля, если они заданы
				if (!tax_code.empty()) product_tax_codes.push_back(tax_code);
				if (excise > 0.001)     product_excises.push_back(excise);
				if (!product_code.empty()) product_product_codes.push_back(product_code);
			}

			// Генерация order_reference
			std::string order_reference = "order_" + std::to_string(order_id) + "_" + std::to_string(rest_id);
			int order_time = static_cast<int>(std::time(nullptr));

			// 4. Формируем payload
			nlohmann::json request_payload = {
				{"merchantAccount", merchant_account},
				{"orderReference", order_reference},
				{"orderDate", order_time},
				{"amount", sum_for_this_restaurant},
				{"currency", "UAH"},
				{"productName", product_names},
				{"productCount", product_counts},
				{"productPrice", product_prices},
				{"language", "UA"},
				{"sendReceipt", true}
			};

			if (!client_email.empty()) {
				request_payload["clientEmail"] = client_email;
			}
			if (!product_tax_codes.empty())    request_payload["productTax"] = product_tax_codes;
			if (!product_excises.empty())      request_payload["productExcise"] = product_excises;
			if (!product_product_codes.empty()) request_payload["productCode"] = product_product_codes;


			// 5. Считаем подпись
			std::ostringstream sign_data;
			sign_data << merchant_account << ';'
				<< order_reference << ';'
				<< order_time << ';'
				<< sum_for_this_restaurant << ';'
				<< "UAH";

			for (size_t i = 0; i < product_names.size(); ++i) {
				sign_data << ';' << product_names[i];
			}
			for (size_t i = 0; i < product_counts.size(); ++i) {
				sign_data << ';' << product_counts[i];
			}
			for (size_t i = 0; i < product_prices.size(); ++i) {
				sign_data << ';' << product_prices[i];
			}

			std::string signature = GenerateHMAC_SHA1(sign_data.str(), secret_key);
			request_payload["merchantSignature"] = signature;

			// 6. Ссылка
			std::string payload_str = request_payload.dump();
			std::string base64_payload = base64_encode(payload_str);
			std::string payment_url = "https://pay.wayforpay.com?data=" + base64_payload;

			// 7. Вставка в БД
			std::string insert_sql = R"sql(
        INSERT INTO order_service_payment (
            order_id, order_reference, payment_url, sum, all_sum, commission,
            per_restaurant_fee, courier_salary, service_profit, merchant_account, full_payload
        ) VALUES (
            $1, $2, $3, $4, $5, $6,
            $7, $8, $9, $10, $11
        )
    )sql";

			std::vector<std::string> insert_params = {
				std::to_string(order_id),
				order_reference,
				payment_url,
				std::to_string(sum_for_this_restaurant),         // сумма по этому ресторану
				std::to_string(all_sum_courier),
				std::to_string(commission_5),
				std::to_string(is_anyplace),
				std::to_string(courier_salary),
				std::to_string(company_profit),
				merchant_account,
				payload_str
			};

			sql.execute(insert_sql, insert_params);

			Log("ORDER", "INFO", "Создана ссылка оплаты для ресторана " + std::to_string(rest_id), clientPtr->_ip, clientPtr->ID);
		}
	#pragma endregion

	#pragma region Service_Payment_URL
		{
			std::string service_merchant = "shokuda_service"; // условный аккаунт ShoKuda
			std::string service_secret = "your_shokuda_secret"; // заменишь на реальный

			std::string order_reference = "order_" + std::to_string(order_id) + "_service";

			int now = static_cast<int>(std::time(nullptr));

			// Получаем email клиента из базы (если есть)
			std::string client_email = "";
			if (clientPtr && clientPtr->ID > 0) {
				std::string email_sql = "SELECT email FROM users WHERE id = $1 LIMIT 1";
				std::vector<std::string> email_params = { std::to_string(clientPtr->ID) };

				auto emails = sql.query<std::string>(email_sql, email_params, [](PGresult* res, int row) {
					return std::string(PQgetvalue(res, row, 0));
					});

				if (!emails.empty()) {
					client_email = emails[0];
				}
			}

			// Формируем подпись и JSON payload
			nlohmann::json payload = {
				{"merchantAccount", service_merchant},
				{"orderReference", order_reference},
				{"orderDate", now},
				{"amount", all_sum_courier},
				{"currency", "UAH"},
				{"productName", {
					"Доставка",
					"Комісія ShoKuda",
					"Мультизаклад" }
				},
				{"productCount", {1, 1, 1}},
				{"productPrice", {
					courier_salary,
					company_profit,
					is_anyplace
				}},
				{"language", "UA"},
				{"sendReceipt", true}  // Включаем отправку чека
			};

			if (!client_email.empty()) {
				payload["clientEmail"] = client_email;
			}

			std::ostringstream sign_data;
			sign_data << service_merchant << ';'
				<< order_reference << ';'
				<< now << ';'
				<< all_sum_courier << ';'
				<< "UAH" << ';'
				<< "Доставка" << ';' << 1 << ';' << courier_salary << ';'
				<< "Комісія ShoKuda" << ';' << 1 << ';' << company_profit << ';'
				<< "Мультизаклад" << ';' << 1 << ';' << is_anyplace;

			std::string signature = GenerateHMAC_SHA1(sign_data.str(), service_secret);
			payload["merchantSignature"] = signature;

			std::string base64_payload = base64_encode(payload.dump());
			std::string payment_url = "https://pay.wayforpay.com?data=" + base64_payload;

			// Запись в order_service_payment
			std::string insert_sql = R"sql(
        INSERT INTO order_service_payment (
            order_id, order_reference, payment_url, sum, all_sum, commission,
            per_restaurant_fee, courier_salary, service_profit, merchant_account, full_payload
        ) VALUES (
            $1, $2, $3, $4, $5, $6,
            $7, $8, $9, $10, $11
        )
    )sql";

			std::vector<std::string> insert_params = {
				std::to_string(order_id),
				order_reference,
				payment_url,
				std::to_string(all_sum_courier - commission_5), // без учёта комиссии
				std::to_string(all_sum_courier), // с учётом
				std::to_string(commission_5),
				std::to_string(is_anyplace),
				std::to_string(courier_salary),
				std::to_string(company_profit),
				service_merchant,
				payload.dump()
			};

			sql.execute(insert_sql, insert_params);

			Log("ORDER", "INFO", "Создана ссылка оплаты ShoKuda (доставка+комиссия)", clientPtr->_ip, clientPtr->ID);
}
	#pragma endregion
	}
	catch (const std::exception& ex)
	{
		Log("ORDER", "ERROR", std::string("Exception: ") + ex.what(), clientPtr->_ip, clientPtr->ID);
		sql.execute("ROLLBACK", {}); // Откат транзакции
		std::vector<uint8_t> response;
		ser.SerializeInt(-1, response);
		SendDataInChunks(clientPtr, response, 500);
		return -1;
	}
	catch (...)
	{
		Log("ORDER", "ERROR", "Unknown exception occurred", clientPtr->_ip, clientPtr->ID);
		sql.execute("ROLLBACK", {});
		std::vector<uint8_t> response;
		ser.SerializeInt(-1, response);
		SendDataInChunks(clientPtr, response, 500);
		return -1;
	}

	return 0;
}

int ShoKuda_server::Recv_order_post(const std::shared_ptr<Client>& clientPtr)
{

	return 0;
}
int ShoKuda_server::Recv_order_shop(const std::shared_ptr<Client>& clientPtr)
{
	return 0;
}
int ShoKuda_server::Recv_order_status_compleation(const std::shared_ptr<Client>& clientPtr, int order_id_from_net)
{
	Log("ORDER", "INFO", "Start receiving order status", clientPtr->_ip, clientPtr->ID);

	try
	{
		size_t offset = 0;

		auto results = sql.query<std::tuple<std::string, std::string>>(
			"SELECT status, courier FROM orders WHERE id = $1",
			{ std::to_string(order_id_from_net) },
			[](PGresult* res, int i) {
				return std::make_tuple(
					std::string(PQgetvalue(res, i, 0)),  // status
					std::string(PQgetvalue(res, i, 1))   // courier (text, но int ID)
				);
			}
		);

		if (results.empty()) {
			Log("ORDER", "FAILED", "Order not found", clientPtr->_ip, clientPtr->ID);
			return -1;
		}

		const std::string& current_status = std::get<0>(results[0]);
		const std::string& courier_id_str = std::get<1>(results[0]);

		// Только если текущий статус — move_to_client
		if (current_status == "move_to_client") {

			// Обновляем статус заказа
			sql.execute(
				"UPDATE orders SET status = $1, updated_at = now() WHERE id = $2",
				{ "success" , std::to_string(order_id_from_net) }
			);

			// Увеличиваем количество выполненных заказов курьера
			if (!courier_id_str.empty()) {
				int courier_id = std::stoi(courier_id_str);
				sql.execute(
					"UPDATE action_card SET completion_order = completion_order + 1 WHERE id = $1",
					{ std::to_string(courier_id) }
				);

				Log("ORDER", "SUCCESS", "Order completed, courier updated", clientPtr->_ip, clientPtr->ID);
			}
			else {
				Log("ORDER", "WARN", "No courier ID attached to order", clientPtr->_ip, clientPtr->ID);
			}
		}
		else {
			Log("ORDER", "WARN", "Order status is not 'move_to_client', skipping", clientPtr->_ip, clientPtr->ID);
		}

		// Отправим подтверждение клиенту
		std::vector<uint8_t> response;
		ser.SerializeInt(1, response);
		SendDataInChunks(clientPtr, response, 500);

		return 0;
	}
	catch (const std::exception& ex)
	{
		Log("ORDER", "FAILED", "Exception: " + std::string(ex.what()), clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		SendDataInChunks(clientPtr, errorResponse,500);
		return -1;
	}
	catch (...)
	{
		Log("ORDER", "FAILED", "Unknown error", clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> errorResponse;
		ser.SerializeInt(-1, errorResponse);
		SendDataInChunks(clientPtr, errorResponse, 500);
		return -1;
	}
}
int ShoKuda_server::Recv_order_feedback(const std::shared_ptr<Client>& clientPtr, int order_id)
{
	Log("FEEDBACK", "INFO", "Start receiving courier feedback", clientPtr->_ip, clientPtr->ID);

	try
	{
		size_t offset = 8;
		auto& data = clientPtr->recv_full;

		// Получаем рейтинг
		int rating_curier = ser.DeserializeInt(data, offset);
		int rating_place = ser.DeserializeInt(data, offset);
		std::string comment = ser.DeserializeString(data, offset);

		Log("FEEDBACK", "DATA", "Rating = " + std::to_string(rating_curier), clientPtr->_ip, clientPtr->ID);
		Log("FEEDBACK", "DATA", "Comment = " + comment, clientPtr->_ip, clientPtr->ID);

		if (rating_curier < 1 || rating_curier > 5) {
			Log("FEEDBACK", "ERROR", "Invalid rating value", clientPtr->_ip, clientPtr->ID);
			std::vector<uint8_t> errorResponse;
			ser.SerializeInt(-2, errorResponse); // -2 — invalid rating
			SendDataInChunks(clientPtr, errorResponse, 500);
			return -1;
		}

		// Проверим, существует ли такой заказ
		auto order_check = sql.query<int>(
			"SELECT COUNT(*) FROM orders WHERE id = $1",
			{ std::to_string(order_id) },
			[](PGresult* res, int i) {
				return std::stoi(PQgetvalue(res, i, 0));
			}
		);

		if (order_check.empty() || order_check[0] == 0) {
			Log("FEEDBACK", "ERROR", "Order not found", clientPtr->_ip, clientPtr->ID);
			std::vector<uint8_t> errorResponse;
			ser.SerializeInt(-3, errorResponse); // -3 — заказ не найден
			SendDataInChunks(clientPtr, errorResponse, 500);
			return -1;
		}

		// Вставка фидбека
		// Получим courier ID из заказа (поле типа TEXT)
		auto courier_rows = sql.query<std::string>(
			"SELECT courier FROM orders WHERE id = $1",
			{ std::to_string(order_id) },
			[](PGresult* res, int i) {
				return std::string(PQgetvalue(res, i, 0));
			}
		);

		int courier_id = -1;
		if (!courier_rows.empty() && !courier_rows[0].empty()) {
			try {
				courier_id = std::stoi(courier_rows[0]);
				Log("FEEDBACK", "INFO", "Courier ID = " + std::to_string(courier_id), clientPtr->_ip, clientPtr->ID);
			}
			catch (const std::exception& ex) {
				Log("FEEDBACK", "ERROR", "Failed to parse courier ID: " + std::string(ex.what()), clientPtr->_ip, clientPtr->ID);
			}
		}
		else {
			Log("FEEDBACK", "WARN", "Courier field is empty or not found for order ID = " + std::to_string(order_id), clientPtr->_ip, clientPtr->ID);
		}

		sql.execute(
			"INSERT INTO client_curier_feedback (order_id, curier_rating, comment, courier_id) VALUES ($1, $2, $3, $4)",
			{
				std::to_string(order_id),
				std::to_string(rating_curier),
				comment,
				std::to_string(courier_id)
			}
		);

		Log("FEEDBACK", "SUCCESS", "Feedback saved", clientPtr->_ip, clientPtr->ID);

		std::vector<uint8_t> response;
		ser.SerializeInt(1, response); // успех
		SendDataInChunks(clientPtr, response, 500);

		clientPtr->recv_full.clear();
		return 0;
	}
	catch (const std::exception& ex)
	{
		Log("FEEDBACK", "ERROR", std::string("Exception: ") + ex.what(), clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> response;
		ser.SerializeInt(-1, response);
		SendDataInChunks(clientPtr, response, 500);
		return -1;
	}
	catch (...)
	{
		Log("FEEDBACK", "ERROR", "Unknown exception", clientPtr->_ip, clientPtr->ID);
		std::vector<uint8_t> response;
		ser.SerializeInt(-1, response);
		SendDataInChunks(clientPtr, response, 500);
		return -1;
	}
}
#pragma endregion
