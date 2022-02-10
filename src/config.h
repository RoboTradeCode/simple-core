#ifndef TRADE_CORE_CONFIG_H
#define TRADE_CORE_CONFIG_H


#include <string>
#include <vector>
#include <toml++/toml.h>

extern const char* DEFAULT_SELL_COEFFICIENT;
extern const char* DEFAULT_BUY_COEFFICIENT;
extern const int DEFAULT_SLEEP_MS;
extern const char* DEFAULT_SUBSCRIBER_CHANNEL;
extern const char* DEFAULT_PUBLISHER_CHANNEL;
extern const int DEFAULT_ORDERBOOKS_STREAM_ID;
extern const int DEFAULT_BALANCES_STREAM_ID;
extern const int DEFAULT_GATEWAY_STREAM_ID;
extern const int DEFAULT_METRICS_STREAM_ID;
extern const int DEFAULT_ERRORS_STREAM_ID;
extern const int DEFAULT_BUFFER_SIZE;

// Структура конфигурации ядра
struct core_config
{
    struct exchange
    {
        std::string sell_coefficient;
        std::string buy_coefficient;
    } exchange;
    struct aeron
    {
        int idle_strategy_sleep_ms{};
        struct orderbooks
        {
            std::string channel;
            int stream_id;
            std::vector<std::string> destinations;
        } orderbooks;
        struct balances
        {
            std::string channel;
            int stream_id;
            std::vector<std::string> destinations;
        } balances;
        struct gateway
        {
            std::string channel;
            int stream_id;
            int buffer_size;
        } gateway;
        struct metrics
        {
            std::string channel;
            int stream_id;
            int buffer_size;
        } metrics;
        struct errors
        {
            std::string channel;
            int stream_id;
            int buffer_size;
        } errors;
    } aeron;
};

/**
 * Преобразует файл конфигурации в структуру, понятную ядру
 *
 * @param file_path Путь к файлу конфигурации в формате TOML
 * @return Конфигурация ядра
 */
core_config parse_config(std::string_view);


#endif  // TRADE_CORE_CONFIG_H
