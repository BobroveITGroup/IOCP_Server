#pragma once

#include <iostream>
#include <string>
#include <set>
#include <sstream>      // std::ostringstream
#include <iomanip>      // std::hex, std::setw, std::setfill
#include <curl/curl.h>

std::string EscapeJsonString(const std::string& input) {
    std::ostringstream ss;
    for (char c : input) {
        switch (c) {
        case '\"': ss << "\\\""; break;
        case '\\': ss << "\\\\"; break;
        case '\b': ss << "\\b"; break;
        case '\f': ss << "\\f"; break;
        case '\n': ss << "\\n"; break;
        case '\r': ss << "\\r"; break;
        case '\t': ss << "\\t"; break;
        default:
            if ('\x00' <= c && c <= '\x1f') {
                ss << "\\u"
                    << std::hex << std::setw(4) << std::setfill('0') << (int)c;
            }
            else {
                ss << c;
            }
        }
    }
    return ss.str();
}

bool SendTelegramMessage(const std::string& botToken, const std::string& chatID, const std::string& message, int64_t messageThreadID, const std::string& replyMarkupJson)
{
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string url = "https://api.telegram.org/bot" + botToken + "/sendMessage";

    // Формируем JSON тело запроса
    std::string jsonBody = "{";
    jsonBody += "\"chat_id\":\"" + chatID + "\",";
    jsonBody += "\"text\":\"" + EscapeJsonString(message) + "\",";
    jsonBody += "\"parse_mode\":\"Markdown\"";

    if (messageThreadID != 0) {
        jsonBody += ",\"message_thread_id\":" + std::to_string(messageThreadID);
    }

    if (!replyMarkupJson.empty()) {
        jsonBody += ",\"reply_markup\":" + replyMarkupJson;
    }

    jsonBody += "}";

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}

/*
					ReportEntry e{};
					// Заполняем дату — 20 мая 2025
					std::tm dt{};
					dt.tm_year = 2025 - 1900;
					dt.tm_mon = 5 - 1;     // месяцы 0–11
					dt.tm_mday = 20;
					e.date = dt;

					e.norm_orders = id_order;
					e.courier_fio = client->Name_Surname_MiddleName_Curier;
					e.card_number = client->Card_Number;
					e.order_type = 3;     // 1 = заведения
					e.total_sum = totalSum;
					e.company_sum = company;
					e.courier_sum = curier;
					e.comments = u8"";

					// 3. Добавляем запись в файл
					excelReport.add_entry(e);

					// 4. Сразу сохраняем (по желанию)
					excelReport.save();

					std::string botToken = "7726203986:AAH3_mvYs3pBoN2VkEkjSbjNl-UM9m80By8";  // Твой токен
					std::string chatID = "-1002384443228";  // Sho?Kuda?_Curiers
					std::string chatID_2 = "-4925755351";  // Sho?Kuda?_Curiers

					// Підготовка рядка для Telegram
					Restaurant res = dbManager.GetRestaurantByID(restarauntID);
					ClientUser user = dbManager.GetClientUserByID(client->ID); // Получен ранее, например, по userID

					std::ostringstream oss;
					oss << u8"🛍️ *Нове замовлення з магазину*:\n"
						<< u8"🔢 Номер замовлення: *" << id_order << u8"*\n\n"

						<< u8"🏪 Магазин: " << res.name << u8"\n"
						<< u8"📍 Адреса магазину: " << res.address << u8"\n"
						<< u8"📞 Телефон: " << res.phonenumber << u8"\n"
						<< u8"🌐 Координати магазину: "
						<< "https://www.google.com/maps/search/?api=1&query=" << res.latitude << "," << res.longitude << "\n\n"

						<< u8"💳 Кур'єр: " << client->Name_Surname_MiddleName_Curier
						<< u8", картка: `" << client->Card_Number << u8"`\n\n"

						<< u8"👤 Дані клієнта:\n"
						<< u8"• ПІБ: *" << user.surname << " " << user.name << " " << user.middlename << u8"*\n"
						<< u8"• Нікнейм: *" << user.nickname << u8"*\n"
						<< u8"• Телефон: *" << user.telephonenumber << u8"*\n\n"

						<< u8"🚚 Адреса доставки: *" << address << u8"*\n"
						<< u8"📍 Координати доставки: "
						<< "https://www.google.com/maps/search/?api=1&query=" << latitude << "," << longitude << u8"\n"
						<< u8"⏰ Час доставки: *" << time << u8"*\n";

					// Додаємо коментар лише якщо він не порожній
					if (!comment.empty()) {
						oss << u8"\n📝 Коментар до замовлення:\n_" << comment << "_";
					}

					std::string message = oss.str();

					// Предположим, что ты узнал, что нужный topic имеет message_thread_id = 12345
					int64_t ordersTopicThreadID = 2;

					std::ostringstream oss_telegram;
					oss_telegram << u8"{\n"
						<< u8"  \"inline_keyboard\": [\n"
						<< u8"    [ { \"text\": \"Еду за товаром\", \"callback_data\": \"post:" << id_order << u8":2\" } ],\n"
						<< u8"    [ { \"text\": \"Еду к клиенту\", \"callback_data\": \"post:" << id_order << u8":3\" } ]\n"
						<< u8"  ]\n"
						<< u8"}";


					std::string replyMarkupJson = oss_telegram.str();

					bool sent = SendTelegramMessage(botToken, chatID, message, 2, replyMarkupJson);
					if (sent == true) {
						printf("Сообщение успешно отправлено\n");
					}
					else {
						printf("Ошибка при отправке сообщения\n");
					}
				}
*/