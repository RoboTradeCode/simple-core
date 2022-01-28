#include "parsers.h"

/**
 * Преобразовать строку в JSON
 *
 * @param message Сообщение
 * @return        JSON
 */
Json::Value json(const std::string& message)
{
    Json::CharReaderBuilder builder{};
    auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
    Json::Value root{};
    std::string errors{};
    reader->parse(message.c_str(), message.c_str() + message.length(), &root, &errors);

    return root;
}
