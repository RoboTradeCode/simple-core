#ifndef TRADE_CORE_CORE_H
#define TRADE_CORE_CORE_H

#include <functional>
#include <simdjson.h>
#include <chrono>
//#include <uuid/uuid.h>
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
//#include <sentry.h>
#include "json.hpp"
#include <Subscriber.h>
#include <Publisher.h>
#include "config.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include "error.hpp"
#include "HTTP.hpp"


namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;
namespace ssl   = boost::asio::ssl;
using     tcp   = boost::asio::ip::tcp;

using JSON = nlohmann::json;

/**
 * Торговое ядро
 *
 * @note Использует протокол Aeron, медиа-драйвер которого заранее должен быть запущен
 */
class core : public std::enable_shared_from_this<core>
{
    // Каналы Aeron
    std::shared_ptr<Subscriber> _orderbooks_channel;
    std::shared_ptr<Subscriber> _balance_channel;
    std::shared_ptr<Subscriber> _order_status_channel;
    std::shared_ptr<Publisher>  _gateway_channel;
    std::shared_ptr<Publisher>  _log_channel;

    //std::shared_ptr<Publisher> metrics_channel;     // TODO: Отправлять метрики
    //std::shared_ptr<Publisher> errors_channel;

    // конфиг по умолчанию (всегда лежит вместе с исполняемым файлом)
    core_config                 _default_config;
    // рабочий конфиг
    core_config                 _work_config;

    // Стратегия ожидания Aeron
    aeron::SleepingIdleStrategy idle_strategy;

    // Коэффициенты для вычисления цены ордеров
    dec_float                   _SELL_RATIO;
    dec_float                   _BUY_RATIO;

    // Коэффициенты для вычисления границ удержания ордеров
    dec_float                   _LOWER_BOUND_RATIO;
    dec_float                   _UPPER_BOUND_RATIO;

    // Последние данные о балансе
    //std::map<std::string, dec_float> _balance;
    std::map<std::string, double> _balance;

    // Последние данные о ордербуках
    std::map<std::string, std::map<std::string, std::pair<dec_float, dec_float>>> _orderbooks;

    // словарь рынков
    // ключ "BTC/USDT" - значение кортеж (пара: базовый ассет и котируемый ассет, price_increment, amount_increment)
    std::map<std::string,                               // валютная пара
           std::tuple<
                  std::pair<std::string, std::string>,  // базовый ассет/котируемый ассет
                  std::pair<dec_float, dec_float>,      // граница min_ask/max_ask
                  std::pair<dec_float, dec_float>,      // граница min_bid/max_bid
                  double,                               // price_increment
                  double                                // amount_increment
    >> _markets;

    std::map<std::string, std::pair<int64_t, bool>> _orders_for_sell;
    //std::map<std::string, std::pair<std::string, bool>> _orders_for_sell;
    std::map<std::string, std::pair<int64_t, bool>> _orders_for_buy;
    //std::map<std::string, std::pair<std::string, bool>> _orders_for_buy;

    // словарь идентификаторов ордеров
    std::map<std::string,               // клиентский идентификатор ордера
            std::tuple<
                std::string,            // валютная пара
                std::string,            // сделка (sell, buy)
                std::chrono::time_point<std::chrono::system_clock>,// время создания ордера
                bool                    // флаг отправки запроса статуса ордера
    >> _clients_id;

    // словарь для контроля отмены ордеров (на случай, если мы не получим ответ от гейта)
    std::map<int64_t,              // идентификатор ордера на отмену
            std::tuple<
                std::string,            // валютная пара
                std::string,            // сделка (sell, buy)
                std::chrono::time_point<std::chrono::system_clock>,// время создания ордера
                bool                    // флаг отправки запроса статуса ордера
    >> _cancel_id;

    // минимиальные сделки
    std::map<std::string, double> _min_deal_amounts;

    // флаг наличия балансов
    bool _has_balance = false;

    // Логгеры
    std::shared_ptr<spdlog::logger> _general_logger;
    std::shared_ptr<spdlog::logger> _logs_logger;
    std::shared_ptr<spdlog::logger> _balances_logger;
    std::shared_ptr<spdlog::logger> _orders_logger;
    std::shared_ptr<spdlog::logger> _errors_logger;
    std::shared_ptr<spdlog::logger> _orderbook_logger;

    // константы ошибок aeron
    const char* BACK_PRESSURED_DESCRIPTION     = "Offer failed due to back pressure.";
    const char* NOT_CONNECTED_DESCRIPTION      = "Offer failed because publisher is not connected to a core.";
    const char* ADMIN_ACTION_DESCRIPTION       = "Offer failed because of an administration action in the system.";
    const char* PUBLICATION_CLOSED_DESCRIPTION = "Offer failed because publication is closed.";
    const char* UNKNOWN_DESCRIPTION            = "Offer failed due to unknkown reason.";

    // содержит предыдущее успешно отправленную команду в гейт
    std::string     _prev_command_message = "none";
    // содержит предыдущую успешно отправленную команду на создание ордера в лог
    std::string     _prev_create_order_log_message = "none";
    // содержит предыдущую успешно отправленную команду на отмену ордера в лог
    std::string     _prev_cancel_order_log_message = "none";
    // флаг успешного получения конфига
    bool        _config_was_received = false;
    std::chrono::time_point<std::chrono::system_clock> _snap_time;
    std::chrono::time_point<std::chrono::system_clock> _delay_time;

    /**
     * Функция обратного вызова для обработки баланса
     *
     * @param message Баланс в формате JSON
     */
    void balance_handler(std::string_view message_);

    /**
     * Функция обратного вызова для обработки биржевых стаканов
     *
     * @param message Биржевой стакан в формате JSON
     */
    void orderbooks_handler(std::string_view message_);

    /**
     * Функция обратного вызова для обработки статуса ордеров
     *
     * @param message стаус ордера в формате JSON
     */
    void order_status_handler(std::string_view message_);
    /**
     * Проверить условия для создания и отмены ордеров
     */
    void process_orders();

    /**
     * Рассчитать среднее арифметическое лучших ордеров для тикера
     *
     * @param ticker Тикер
     * @return Пара, содержащая цену покупки и продажи соответственно
     */
    std::pair<dec_float, dec_float> avg_orderbooks(const std::string& ticker_);

    /**
     * Создать ордер
     *
     * @param side Тип ордера
     * @param price Цена
     * @param quantity Объём
     */
    std::string create_order(std::string_view side_, const std::string& symbol_, const dec_float& price_, const dec_float& quantity_, double price_precission_, double amount_precission_);

    /**
     * Отменить все ордера
     *
     * @param side Тип ордера
     */
    void cancel_all_orders(std::string_view side_);
    /**
     * Отменить ордер
     *
     * @param side Тип ордера
     */
    void cancel_order(std::string_view side_, std::string ticker_, /*int64_t id_*/std::string id_);

    // отправляет запрос на отмену всех ордеров
    void    send_cancel_all_orders_request();
    // отправляет запрос на получение балансов
    void    send_get_balances_request();
    // отправляет запрос на получение статуса ордера
    void    send_get_order_status_request(const std::string& order_id_);
    void    send_get_order_status_by_client_id_request(const std::string& client_id_);
    // обрабатывает и логирует ошибку от каналов aeron
    void    processing_error(std::string_view error_source_, const std::string& prev_messsage_, const std::int64_t& error_code_);
    // загружет конфиг из json
    bool    load_config_from_json(const std::string& message_, bss::error& error_) noexcept;
    //  генерирует идентификатор ордера
    std::string    create_client_id();

public:
    // для получения конфига с сервера
    util::HTTPSession http_session;
    /**
     * Создать экземпляр торгового ядра и подключиться к каналам Aeron
     *
     * @param config_file_path Путь к файлу конфигурации в формате TOML
     */
    explicit core(std::string config_file_path_);

    /**
     * Проверить каналы Aeron на наличие новых сообщений
     */
    void poll();

    bool        create_aeron_agent_channels();
    // загружает конфиг из файла
    bool        load_config_from_file(bss::error& error_);
    // получает источник получения конфига
    std::string get_config_source();
    // посылает ошибку в консоль и лог
    void        send_error(std::string_view error_);
    // посылает сообщение в консоль и лог
    void        send_message(std::string_view message_);
    // подготавливает к запуску
    bool        prepatation_for_launch(bss::error& error_);
    // проверяет получен ли конфиг
    bool        has_config();
    // получает конфиг непосредственно с сервера
    bool        get_config_from_api(bss::error& error_);
};


#endif  // TRADE_CORE_CORE_H
