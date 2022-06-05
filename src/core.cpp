#include <boost/json/src.hpp>
#include "core.hpp"

/**
 * Создать экземпляр торгового ядра и подключиться к каналам Aeron
 *
 * @param config_file_path Путь к файлу конфигурации в формате TOML
 */
core::core(std::string config_file_path_)
    : idle_strategy(std::chrono::milliseconds(DEFAULT_IDLE_STRATEGY_SLEEP_MS)),
      orderbooks_logger(spdlog::get("orderbooks")),
      _balances_logger(spdlog::get("balances")),
      _orders_logger(spdlog::get("orders")),
      _errors_logger(spdlog::get("errors")),
      _general_logger(spdlog::get("general"))
{
    _general_logger->info("Core starting...");
    // Получение дефолтной конфигурации
    _default_config = parse_config(config_file_path_);
}
//---------------------------------------------------------------
// подготавливает к запуску
//---------------------------------------------------------------
bool core::prepatation_for_launch() {
    // Сокращения для удобства доступа
    //auto exchange       = _work_config.exchange;
    auto aeron          = _work_config.aeron;
    auto subscribers    = aeron.subscribers;
    auto publishers     = aeron.publishers;
    auto gateway        = publishers.gateway;
    //auto metrics        = publishers.metrics;
    //auto errors         = publishers.errors;

    // Инициализация каналов Aeron
    _orderbooks_channel = std::make_shared<Subscriber>(
        [&](std::string_view message_)
        { shared_from_this()->orderbooks_handler(message_); },
        subscribers.orderbooks.channel,
        subscribers.orderbooks.stream_id
    );
    _balance_channel = std::make_shared<Subscriber>(
        [&](std::string_view message_)
        { shared_from_this()->balance_handler(message_); },
        subscribers.balances.channel,
        subscribers.balances.stream_id
    );
    _order_status_channel = std::make_shared<Subscriber>(
        [&](std::string_view message_)
        { shared_from_this()->order_status_handler(message_); },
        subscribers.order_statuses.channel,
        subscribers.order_statuses.stream_id
    );
    _gateway_channel = std::make_shared<Publisher>(gateway.channel, gateway.stream_id);
    //metrics_channel = std::make_shared<Publisher>(metrics.channel, metrics.stream_id, metrics.buffer_size);
    //errors_channel = std::make_shared<Publisher>(errors.channel, errors.stream_id, errors.buffer_size);

    // Подписка на Publisher'ов
 /*   for (const std::string& channel: subscribers.orderbooks.destinations)
        _orderbooks_channel->add_destination(channel);
    for (const std::string& channel: subscribers.balances.destinations)
        _balance_channel->add_destination(channel);
*/
    // Инициализация стратегии ожидания
    idle_strategy = aeron::SleepingIdleStrategy(std::chrono::milliseconds(subscribers.idle_strategy_sleep_ms));

    // Инициализация пороговых значений для инструментов
    //BTC_THRESHOLD  = dec_float(exchange.btc_threshold);
    //USDT_THRESHOLD = dec_float(exchange.usdt_threshold);


    // Инициализация коэффициентов для вычисления цены ордеров
    //SELL_RATIO = dec_float(exchange.sell_ratio);
    _SELL_RATIO = dec_float(1.0015);
    //BUY_RATIO  = dec_float(exchange.buy_ratio);
    _BUY_RATIO  = dec_float(0.9985);

    // Инициализация коэффициентов для вычисления границ удержания ордеров
    //LOWER_BOUND_RATIO = dec_float(exchange.lower_bound_ratio);
    _LOWER_BOUND_RATIO = dec_float(0.9995);
    //UPPER_BOUND_RATIO = dec_float(exchange.upper_bound_ratio);
    _UPPER_BOUND_RATIO = dec_float(1.0005);

    // получим шаг цены и объема
    //_price_precission = utils::get_precission(exchange.price_increment);
    //_size_precision   = utils::get_precission(exchange.size_increment);

    // отменяем все ордера
 /*   send_cancel_all_orders_request();
    // запрашиваем баланс
    send_get_balances_request();*/
    return true;
}
//---------------------------------------------------------------
// загружает конфиг из файла
//---------------------------------------------------------------
bool core::load_config_from_file(bss::error& error_){
    // создаем парсер
    simdjson::dom::parser parser;
    // скажем парсеру, чтобы он подготовил буфер для своих внутренних нужд (если будет меньше 0x08, то будет ошибка)
    auto &&error_code = parser.allocate(0x1000, 0x12);
    // если буфер успешно выделен
    if (simdjson::SUCCESS == error_code) {
        auto load_result = simdjson::padded_string::load("config.json");
        // если файл загрузили
        if (simdjson::SUCCESS == load_result.error()) {
            auto result = parser.parse(load_result.value().data(), load_result.value().size(), false);
            if (simdjson::SUCCESS == result.error()) {
        //                if (auto is_new_element{result["is_new"].get_bool()}; simdjson::SUCCESS == is_new_element.error()) {
        //                    if (is_new_element.value() == true) {
        //                        std::cout << "Конфигурация обновилась." << std::endl;
        //                    }
        //                } else {
        //                    //error_.describe("При загрузке конфигурации в теле json не найден оъект is_new.");
        //                    // скорее всего дальше незачем парсить
        //                    //return false;
        //                }
                // получим рынки, с которыми предстоит работать
                if (auto markets_array{result["data"]["markets"].get_array()}; simdjson::SUCCESS == markets_array.error()){
                    for (auto market : markets_array) {
                        std::string common_symbol;
                        std::string base_asset;
                        std::string quote_asset;
                        double      limits;
                        double      price_increment;
                        double      amount_increment;
                        if (auto common_symbol_element{market["common_symbol"].get_string()}; simdjson::SUCCESS == common_symbol_element.error()) {
                            //_work_config._markets.push_back(common_symbol_element.value());
                            common_symbol = common_symbol_element.value();
                        } else {}
                        if (auto base_asset_element{market["base_asset"].get_string()}; simdjson::SUCCESS == base_asset_element.error()) {
                            base_asset = base_asset_element.value();
                        } else {}
                        if (auto quote_asset_element{market["quote_asset"].get_string()}; simdjson::SUCCESS == quote_asset_element.error()) {
                            quote_asset = quote_asset_element.value();
                        }
                        if (auto price_increment_element{market["price_increment"].get_double()}; simdjson::SUCCESS == price_increment_element.error()) {
                            price_increment = price_increment_element.value();
                        }
                        if (auto amount_increment_element{market["amount_increment"].get_double()}; simdjson::SUCCESS == amount_increment_element.error()) {
                            amount_increment = amount_increment_element.value();
                        }
                        _orders_for_sell[common_symbol] = std::make_pair(0, false);
                        _orders_for_buy[common_symbol] = std::make_pair(0, false);
                        _markets[common_symbol] = std::make_tuple(std::make_pair(base_asset, quote_asset),
                                                                  std::make_pair(dec_float(0), dec_float(0)),
                                                                  std::make_pair(dec_float(0), dec_float(0)),
                                                                  price_increment, amount_increment);
                    }
                } else {
                    error_.describe("При загрузке конфигурации в теле json не найден объект \"maktets\".");
                }

                // получим часть пути для сокращения полного пути до элементов
                if (auto cfg = result["data"]["configs"]["core_config"]; simdjson::SUCCESS == cfg.error()) {
                    // получаем имя биржи
                    // получаем данные для канала команд в гейт
                    if (auto gate_channel_element{cfg["aeron"]["publishers"]["gate"]["channel"].get_string()}; simdjson::SUCCESS == gate_channel_element.error()) {
                         _work_config.aeron.publishers.gateway.channel = gate_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][publishers][gate][channel]\".");
                    }
                    if (auto gate__stream_element{cfg["aeron"]["publishers"]["gate"]["stream_id"].get_int64()}; simdjson::SUCCESS == gate__stream_element.error()) {
                        _work_config.aeron.publishers.gateway.stream_id = gate__stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][publishers][gate][stream_id]\".");
                    }
                    // получаем данные для канала ордербуков
                    if (auto orderbook_channel_element{cfg["aeron"]["subscribers"]["orderbooks"]["channel"].get_string()}; simdjson::SUCCESS == orderbook_channel_element.error()) {
                        _work_config.aeron.subscribers.orderbooks.channel = orderbook_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][subscribers][orderbooks][channel]\".");
                    }
                    if (auto orderbooks_stream_id_element{cfg["aeron"]["subscribers"]["orderbooks"]["stream_id"].get_int64()}; simdjson::SUCCESS == orderbooks_stream_id_element.error()) {
                        _work_config.aeron.subscribers.orderbooks.stream_id = orderbooks_stream_id_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][subscribers][orderbooks][stream_id]\".");
                    }
                    if (auto orderbooks_destination_element{cfg["aeron"]["subscribers"]["orderbooks"]["destinations"].get_array()}; simdjson::SUCCESS == orderbooks_destination_element.error()) {
                        for (auto destination : orderbooks_destination_element) {
                            _work_config.aeron.subscribers.orderbooks.destinations.emplace_back(destination);
                        }
                    } else {}
                    // получаем данные для канала балансов
                    if (auto balances_channel_element{cfg["aeron"]["subscribers"]["balance"]["channel"].get_string()}; simdjson::SUCCESS == balances_channel_element.error()) {
                        _work_config.aeron.subscribers.balances.channel = balances_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][balance][channel]\".");
                    }
                    if (auto balances_stream_element{cfg["aeron"]["subscribers"]["balance"]["stream_id"].get_int64()}; simdjson::SUCCESS == balances_stream_element.error()) {
                        _work_config.aeron.subscribers.balances.stream_id = balances_stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][balance][stream_id]\".");
                    }
                    if (auto balances_destination_element{cfg["aeron"]["subscribers"]["balance"]["destinations"].get_array()}; simdjson::SUCCESS == balances_destination_element.error()) {
                        for (auto destination : balances_destination_element) {
                            _work_config.aeron.subscribers.balances.destinations.emplace_back(destination);
                        }
                    }
                    // получаем данные для канала логов
        //            if (auto log_channel_element{cfg["aeron"]["publishers"]["logs"]["channel"].get_string()}; simdjson::SUCCESS == log_channel_element.error()) {
        //                _work_config.aeron_core.publishers.logs.channel = log_channel_element.value();
        //            } else {
        //                error_.describe("При загрузке конфигурации в теле json не найден оъект \"log channel\".");
        //            }
        //            if (auto log_stream_element{cfg["aeron"]["publishers"]["logs"]["stream_id"].get_int64()}; simdjson::SUCCESS == log_stream_element.error()) {
        //                _work_config.aeron_core.publishers.logs.stream_id = log_stream_element.value();
        //            } else {
        //                error_.describe("При загрузке конфигурации в теле json не найден оъект \"log stream_id\".");
        //            }
                    // получаем данные для канала статуса ордеров
                    if (auto order_statuses_channel_element{cfg["aeron"]["subscribers"]["orders_statuses"]["channel"].get_string()}; simdjson::SUCCESS == order_statuses_channel_element.error()) {
                        _work_config.aeron.subscribers.order_statuses.channel = order_statuses_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][orders_statuses][channel]\".");
                    }
                    if (auto order_statuses_stream_element{cfg["aeron"]["subscribers"]["orders_statuses"]["stream_id"].get_int64()}; simdjson::SUCCESS == order_statuses_stream_element.error()) {
                        _work_config.aeron.subscribers.order_statuses.stream_id = order_statuses_stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][orders_statuses][stream_id]\".");
                    }
                } else {
                    error_.describe("При загрузке конфигурации в теле json не найден оъект [\"data\"][\"gate_config\"].");
                }
            } else {
                error_.describe(fmt::format("Ошибка разбора json фрейма в процессе парсинга конфига. Код ошибки: {}.", result.error()));
            }
        } else {
            error_.describe("Ошибка загрузки файла конфигурации.");
        }
    } else {
        error_.describe("Ошибка инициализации парсера simdjson (внутренний буфер не выделился).");
    }
    if (not error_) {
        // если не было ошибок, то считаем, что конфиг получен
        _config_was_received = true;
        return true;
    }
    else
        return false;
}
//---------------------------------------------------------------
// загружает конфиг из json
//---------------------------------------------------------------
bool core::load_config_from_json(const std::string& message_, bss::error &error_) noexcept{
    // создаем парсер
    simdjson::dom::parser parser;
    // скажем парсеру, чтобы он подготовил буфер для своих внутренних нужд (если будет меньше 0x08, то будет ошибка)
    auto &&error_code = parser.allocate(0x1000, 0x12);
    // если буфер успешно выделен
    if (simdjson::SUCCESS == error_code) {
        //auto load_result = simdjson::padded_string::load("config.json");
        // если файл загрузили
        //if (simdjson::SUCCESS == load_result.error()) {
            auto result = parser.parse(message_.c_str(), message_.size(), false);
            if (simdjson::SUCCESS == result.error()) {
        //                if (auto is_new_element{result["is_new"].get_bool()}; simdjson::SUCCESS == is_new_element.error()) {
        //                    if (is_new_element.value() == true) {
        //                        std::cout << "Конфигурация обновилась." << std::endl;
        //                    }
        //                } else {
        //                    //error_.describe("При загрузке конфигурации в теле json не найден оъект is_new.");
        //                    // скорее всего дальше незачем парсить
        //                    //return false;
        //                }
                // получим рынки, с которыми предстоит работать
                if (auto markets_array{result["data"]["markets"].get_array()}; simdjson::SUCCESS == markets_array.error()){
                    for (auto market : markets_array) {
                        std::string common_symbol;
                        std::string base_asset;
                        std::string quote_asset;
                        double      limits;
                        double      price_increment;
                        double      amount_increment;
                        if (auto common_symbol_element{market["common_symbol"].get_string()}; simdjson::SUCCESS == common_symbol_element.error()) {
                            //_work_config._markets.push_back(common_symbol_element.value());
                            common_symbol = common_symbol_element.value();
                        } else {}
                        if (auto base_asset_element{market["base_asset"].get_string()}; simdjson::SUCCESS == base_asset_element.error()) {
                            base_asset = base_asset_element.value();
                        } else {}
                        if (auto quote_asset_element{market["quote_asset"].get_string()}; simdjson::SUCCESS == quote_asset_element.error()) {
                            quote_asset = quote_asset_element.value();
                        }
                        if (auto price_increment_element{market["price_increment"].get_double()}; simdjson::SUCCESS == price_increment_element.error()) {
                            price_increment = price_increment_element.value();
                        }
                        if (auto amount_increment_element{market["amount_increment"].get_double()}; simdjson::SUCCESS == amount_increment_element.error()) {
                            amount_increment = amount_increment_element.value();
                        }
                        _orders_for_sell[common_symbol] = std::make_pair(0, false);
                        _orders_for_buy[common_symbol] = std::make_pair(0, false);
                        _markets[common_symbol] = std::make_tuple(std::make_pair(base_asset, quote_asset),
                                                                  std::make_pair(dec_float(0), dec_float(0)),
                                                                  std::make_pair(dec_float(0), dec_float(0)),
                                                                  price_increment, amount_increment);
                    }
                } else {
                    error_.describe("При загрузке конфигурации в теле json не найден объект \"maktets\".");
                }

                // получим часть пути для сокращения полного пути до элементов
                if (auto cfg = result["data"]["configs"]["core_config"]; simdjson::SUCCESS == cfg.error()) {
                    // получаем имя биржи
                    // получаем данные для канала команд в гейт
                    if (auto gate_channel_element{cfg["aeron"]["publishers"]["gate"]["channel"].get_string()}; simdjson::SUCCESS == gate_channel_element.error()) {
                         _work_config.aeron.publishers.gateway.channel = gate_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][publishers][gate][channel]\".");
                    }
                    if (auto gate__stream_element{cfg["aeron"]["publishers"]["gate"]["stream_id"].get_int64()}; simdjson::SUCCESS == gate__stream_element.error()) {
                        _work_config.aeron.publishers.gateway.stream_id = gate__stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][publishers][gate][stream_id]\".");
                    }
                    // получаем данные для канала ордербуков
                    if (auto orderbook_channel_element{cfg["aeron"]["subscribers"]["orderbooks"]["channel"].get_string()}; simdjson::SUCCESS == orderbook_channel_element.error()) {
                        _work_config.aeron.subscribers.orderbooks.channel = orderbook_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][subscribers][orderbooks][channel]\".");
                    }
                    if (auto orderbooks_stream_id_element{cfg["aeron"]["subscribers"]["orderbooks"]["stream_id"].get_int64()}; simdjson::SUCCESS == orderbooks_stream_id_element.error()) {
                        _work_config.aeron.subscribers.orderbooks.stream_id = orderbooks_stream_id_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][subscribers][orderbooks][stream_id]\".");
                    }
                    if (auto orderbooks_destination_element{cfg["aeron"]["subscribers"]["orderbooks"]["destinations"].get_array()}; simdjson::SUCCESS == orderbooks_destination_element.error()) {
                        for (auto destination : orderbooks_destination_element) {
                            _work_config.aeron.subscribers.orderbooks.destinations.emplace_back(destination);
                        }
                    } else {}
                    // получаем данные для канала балансов
                    if (auto balances_channel_element{cfg["aeron"]["subscribers"]["balance"]["channel"].get_string()}; simdjson::SUCCESS == balances_channel_element.error()) {
                        _work_config.aeron.subscribers.balances.channel = balances_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][balance][channel]\".");
                    }
                    if (auto balances_stream_element{cfg["aeron"]["subscribers"]["balance"]["stream_id"].get_int64()}; simdjson::SUCCESS == balances_stream_element.error()) {
                        _work_config.aeron.subscribers.balances.stream_id = balances_stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][balance][stream_id]\".");
                    }
                    if (auto balances_destination_element{cfg["aeron"]["subscribers"]["balance"]["destinations"].get_array()}; simdjson::SUCCESS == balances_destination_element.error()) {
                        for (auto destination : balances_destination_element) {
                            _work_config.aeron.subscribers.balances.destinations.emplace_back(destination);
                        }
                    }
                    // получаем данные для канала логов
        //            if (auto log_channel_element{cfg["aeron"]["publishers"]["logs"]["channel"].get_string()}; simdjson::SUCCESS == log_channel_element.error()) {
        //                _work_config.aeron_core.publishers.logs.channel = log_channel_element.value();
        //            } else {
        //                error_.describe("При загрузке конфигурации в теле json не найден оъект \"log channel\".");
        //            }
        //            if (auto log_stream_element{cfg["aeron"]["publishers"]["logs"]["stream_id"].get_int64()}; simdjson::SUCCESS == log_stream_element.error()) {
        //                _work_config.aeron_core.publishers.logs.stream_id = log_stream_element.value();
        //            } else {
        //                error_.describe("При загрузке конфигурации в теле json не найден оъект \"log stream_id\".");
        //            }
                    // получаем данные для канала статуса ордеров
                    if (auto order_statuses_channel_element{cfg["aeron"]["subscribers"]["orders_statuses"]["channel"].get_string()}; simdjson::SUCCESS == order_statuses_channel_element.error()) {
                        _work_config.aeron.subscribers.order_statuses.channel = order_statuses_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][orders_statuses][channel]\".");
                    }
                    if (auto order_statuses_stream_element{cfg["aeron"]["subscribers"]["orders_statuses"]["stream_id"].get_int64()}; simdjson::SUCCESS == order_statuses_stream_element.error()) {
                        _work_config.aeron.subscribers.order_statuses.stream_id = order_statuses_stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][orders_statuses][stream_id]\".");
                    }
                } else {
                    error_.describe("При загрузке конфигурации в теле json не найден оъект [\"data\"][\"gate_config\"].");
                }
            } else {
                error_.describe(fmt::format("Ошибка разбора json фрейма в процессе парсинга конфига. Код ошибки: {}.", result.error()));
            }

    } else {
        error_.describe("Ошибка инициализации парсера simdjson (внутренний буфер не выделился).");
    }
    if (not error_) {
        // если не было ошибок, то считаем, что конфиг получен
        _config_was_received = true;
        return true;
    }
    else
        return false;
}
//---------------------------------------------------------------
// отправляет запрос на отмену всех ордеров
//---------------------------------------------------------------
void core::send_cancel_all_orders_request(){

    JSON cancel_orders_request;
    cancel_orders_request["event"]       = "command";
    cancel_orders_request["exchange"]    = "ftx";
    cancel_orders_request["node"]        = "core";
    cancel_orders_request["instance"]    = 1;
    cancel_orders_request["action"]      = "cancel_all_orders";
    cancel_orders_request["message"]     = "";
    cancel_orders_request["algo"]        = "spred_bot";
    cancel_orders_request["timestamp"]   = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    cancel_orders_request["data"]        = nullptr;

    int64_t result = _gateway_channel->offer(cancel_orders_request.dump());
    if (result < 0) {
        processing_error("Ошибка отправки запроса на отмену всех ордеров: ", "none", result);
        if (result == aeron::ADMIN_ACTION) {
            result = _gateway_channel->offer(cancel_orders_request.dump());
            if (result < 0) {
                processing_error("Повторная ошибка отправки запроса на отмену всех ордеров: ", "none", result);
            }
        }
    } else {
        _general_logger->info("Отправлен запрос на отмену всех ордеров.");
    }
}
//---------------------------------------------------------------
// отправляет запрос на получение балансов
//---------------------------------------------------------------
void core::send_get_balances_request(){

    JSON get_balances_request;
    get_balances_request["event"]       = "command";
    get_balances_request["exchange"]    = "ftx";
    get_balances_request["node"]        = "core";
    get_balances_request["instance"]    = 1;
    get_balances_request["action"]      = "get_balances";
    get_balances_request["message"]     = "";
    get_balances_request["algo"]        = "spred_bot";
    get_balances_request["timestamp"]   = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    get_balances_request["data"]        = nullptr;

    int64_t result = _gateway_channel->offer(get_balances_request.dump());
    if (result < 0) {
        processing_error("Ошибка отправки запроса на получение балансов: ", "none", result);
        if (result == aeron::ADMIN_ACTION) {
            result = _gateway_channel->offer(get_balances_request.dump());
            if (result < 0) {
                processing_error("Повторная ошибка отправки запроса на получение балансов: ", "none", result);
            }
        }
    } else {
        _general_logger->info("Отправлен запрос на получение балансов.");
    }
}
//---------------------------------------------------------------
//
//---------------------------------------------------------------
void core::processing_error(std::string_view error_source_, const std::string& prev_messsage_, const std::int64_t& error_code_) {

    _errors_logger->info("Предыдущее успешно отправленное сообщение: {}.", prev_messsage_);
    _errors_logger->error(error_source_);
    if (error_code_ == aeron::BACK_PRESSURED) {
        _errors_logger->error(BACK_PRESSURED_DESCRIPTION);
    } else if (error_code_ == aeron::NOT_CONNECTED) {
        _errors_logger->error(NOT_CONNECTED_DESCRIPTION);
    } else if (error_code_ == aeron::ADMIN_ACTION) {
        _errors_logger->error(ADMIN_ACTION_DESCRIPTION);
    } else if (error_code_ == aeron::PUBLICATION_CLOSED) {
        _errors_logger->error(PUBLICATION_CLOSED_DESCRIPTION);
    } else {
        _errors_logger->error(UNKNOWN_DESCRIPTION);
    }
}
/**
 * Проверить каналы Aeron на наличие новых сообщений
 */
void core::poll()
{
    // Опрос каналов
    int fragments_read_orderbooks     = _orderbooks_channel->poll();
    int fragments_read_balance        = _balance_channel->poll();
    int fragments_read_order_statuses = _order_status_channel->poll();

    // Выполнение стратегии ожидания
    int fragments_read = fragments_read_orderbooks + fragments_read_balance + fragments_read_order_statuses;
    idle_strategy.idle(fragments_read);
}

/**
 * Функция обратного вызова для обработки баланса
 *
 * @param message Баланс в формате JSON
 */
void core::balance_handler(std::string_view message_)
{
    //std::cout << message_ << std::endl;
    _balances_logger->info(message_);

    // Создадим парсер.
    simdjson::ondemand::parser parser;
    simdjson::padded_string json(message_);
    // Получим данные.
    auto parse_result = parser.iterate(json);
    if (simdjson::SUCCESS == parse_result.error()) {
        // проверяем значение события
        if (auto event_element{parse_result["event"].get_string()}; simdjson::SUCCESS == event_element.error()) {
            if (event_element.value() == "data") {
                // проверяем значения action
                if (auto action_element{parse_result["action"].get_string()}; simdjson::SUCCESS == action_element.error()) {
                    if (action_element.value() == "balances") {
                        /*auto obj = parse_result["data"].get_object();
                        for (auto field : obj) {
                            std::cout << field.key() << std::endl;
                        }*/
                        if (auto data_element_objects{parse_result["data"].get_object()}; simdjson::SUCCESS == data_element_objects.error()) {
                            std::string ticker;
                            double free;
                            std::string_view asset_value;
                            for (auto asset : data_element_objects) {
                                //std::cout << asset.unescaped_key() << std::endl;

                                asset_value = asset.unescaped_key();
                                if (auto asset_free_element_array{parse_result["data"][asset_value]["free"].get_double()}; simdjson::SUCCESS == asset_free_element_array.error()) {
                                    free = asset_free_element_array.value();
                                } else {

                                }
                                _balance[std::string(asset_value)] = free;
                            }
                            if (_balance.size() != 0)
                                _has_balance = true;
                        }

                    } else {
                        std::cout << "поле action содержит значение: " << action_element.value() << std::endl;
                    }
                } else {
                    std::cout << "Ошибка получения поля action" << std::endl;
                }
            } else {
                std::cout << "поле event содержит значение: " << event_element.value() << std::endl;
            }
        } else {
            std::cout << "Ошибка получения поля event" << std::endl;
        }
    }
}
/**
 * Функция обратного вызова для обработки статуса ордеров
 *
 * @param message Статус ордера в формате json
 */
void core::order_status_handler(std::string_view message_) {

    //std::cout << "!!!!! order_status_handler: " << message_ << std::endl;
    _general_logger->info(message_);
    // Создадим парсер.
    simdjson::ondemand::parser parser;
    simdjson::padded_string json(message_);
    // Получим данные.
    auto parse_result = parser.iterate(json);
    if (simdjson::SUCCESS == parse_result.error()) {
        // проверяем значение события
        if (auto event_element{parse_result["event"].get_string()}; simdjson::SUCCESS == event_element.error()) {
            if (event_element.value() == "data") {
                std::string side;
                std::string symbol;
                int64_t     id;
                // проверяем значения action
                if (auto action_element{parse_result["action"].get_string()}; simdjson::SUCCESS == action_element.error()) {
                    if (action_element.value() == "order_created") {
                        // был создан ордер
                        if (auto side_element{parse_result["data"]["side"].get_string()}; simdjson::SUCCESS == side_element.error()) {
                            side = side_element.value();
                        } else {}
                        if (auto symbol_element{parse_result["data"]["symbol"].get_string()}; simdjson::SUCCESS == symbol_element.error()) {
                            symbol = symbol_element.value();
                        } else {}
                        if (auto id_element{parse_result["data"]["id"].get_int64()}; simdjson::SUCCESS == id_element.error()) {
                            id = id_element.value();
                        } else {}
                        if (side == "sell") {
                            //_sell_orders[id] = symbol;
                            _orders_for_sell[symbol].first = id;
                            std::cout << "добавили в _sell_orders id: " << id << std::endl;
                        } else if (side == "buy") {
                            //_buy_orders[id] = symbol;
                            _orders_for_buy[symbol].first = id;
                            std::cout << "добавили в _buy_orders id: " << id << std::endl;
                        }
                    } else if (action_element.value() == "order_cancel") {
                        // был отменен ордер
                        if (auto side_element{parse_result["data"]["side"].get_string()}; simdjson::SUCCESS == side_element.error()) {
                            side = side_element.value();
                        } else {}
                        if (auto symbol_element{parse_result["data"]["symbol"].get_string()}; simdjson::SUCCESS == symbol_element.error()) {
                            symbol = symbol_element.value();
                        } else {}
                        if (auto id_element{parse_result["data"]["id"].get_int64()}; simdjson::SUCCESS == id_element.error()) {
                            id = id_element.value();
                        } else {}
                        if (side == "sell") {
                            _orders_for_sell[symbol].first = 0;
                        } else if (side == "buy") {
                              _orders_for_buy[symbol].first = 0;
                        }
                    } else {
                        std::cout << "поле action содержит значение: " << action_element.value() << std::endl;
                    }
                } else {
                    std::cout << "Ошибка получения поля action" << std::endl;
                }
            } else {
                std::cout << "поле event содержит значение: " << event_element.value() << std::endl;
            }
        } else {
            std::cout << "Ошибка получения поля event" << std::endl;
        }
    }
}
/**
 * Функция обратного вызова для обработки биржевых стаканов
 *
 * @param message Биржевой стакан в формате JSON
 */
void core::orderbooks_handler(std::string_view message_)
{
    //std::cout << "--------------------------------------" << std::endl;
    //std::cout << "orderbooks_handler " << message_ << std::endl;
    //std::cout << "--------------------------------------" << std::endl;
    //orderbooks_logger->info(message_);

    // Создадим парсер.
    simdjson::ondemand::parser parser;
    simdjson::padded_string json(message_);
    // Получим данные.
    auto parse_result = parser.iterate(json);
    if (simdjson::SUCCESS == parse_result.error()) {
        // проверяем значение события
        if (auto event_element{parse_result["event"].get_string()}; simdjson::SUCCESS == event_element.error()) {
            if (event_element.value() == "data") {
                // проверяем значения action
                if (auto action_element{parse_result["action"].get_string()}; simdjson::SUCCESS == action_element.error()) {
                    if (action_element.value() == "orderbook") {
                        std::string exchange;
                        std::string ticker;
                        double best_ask;
                        double best_bid;
                        if (auto exchange_element{parse_result["exchange"].get_string()}; simdjson::SUCCESS == exchange_element.error()) {
                            exchange = exchange_element.value();
                        } else {}
                        if (auto data_element_array{parse_result["data"]["asks"].get_array()}; simdjson::SUCCESS == data_element_array.error()) {
                            for (auto data_element : data_element_array)  {
                                best_ask = data_element.get_array().at(0).get_double();
                            }
                        } else {
                            std::cout << "Ошибка получения массива поля [\"data\"][\"asks\"]: " << data_element_array << std::endl;
                        }
                        if (auto data_element_array{parse_result["data"]["bids"].get_array()}; simdjson::SUCCESS == data_element_array.error()) {
                            for (auto data_element : data_element_array)  {
                                best_bid = data_element.get_array().at(0).get_double();
                            }
                        } else {
                            std::cout << "Ошибка получения массива поля [\"data\"][\"bids\"]: " << data_element_array << std::endl;
                        }
                        if (auto symbol_element{parse_result["data"]["symbol"].get_string()}; simdjson::SUCCESS == symbol_element.error()) {
                            ticker = symbol_element.value();
                        } else {}
                        std::cout << exchange << " " << ticker << std::endl;
                        // Обновление сохранённого ордербука
                        _orderbooks[exchange][ticker] = std::make_pair(best_ask, best_bid);

                        // Проверка условий для создания и отмены ордеров (при запуске у нас может не быть данных о балансе,
                        // поэтому функцию нет смысла вызывать)
                        if (_has_balance)
                            process_orders();
                    }
                }
            }
        }
    }
}

/**
 * Проверить условия для создания и отмены ордеров
 */
void core::process_orders()
{
    for (auto &[symbol, tpl]:_markets) {
        //std::cout << symbol << std::endl;
        // Получение среднего арифметического ордербуков BTC-USDT
        std::pair<dec_float, dec_float> avg = avg_orderbooks(symbol);
        dec_float avg_ask = avg.first;
        dec_float avg_bid = avg.second;

        // Расчёт стоимости возможных ордеров
        dec_float sell_price    = avg_ask * _SELL_RATIO;
        dec_float buy_price     = avg_bid * _BUY_RATIO;

        dec_float sell_quantity = _balance[std::get<0>(tpl).first];
        dec_float buy_quantity  = _balance[std::get<0>(tpl).second] / sell_price;
//        std::cout << "Объем продажи: " << sell_quantity << ". Объем покупки: " << buy_quantity << std::endl;

        dec_float min_ask = avg_ask * _LOWER_BOUND_RATIO;
        dec_float max_ask = avg_ask * _UPPER_BOUND_RATIO;
        dec_float min_bid = avg_bid * _LOWER_BOUND_RATIO;
        dec_float max_bid = avg_bid * _UPPER_BOUND_RATIO;

        auto&[id_sell_order, has_sell_orders] = _orders_for_sell[symbol];
        auto&[id_buy_order, has_buy_orders]   = _orders_for_buy[symbol];

        double threshold = std::get<4>(tpl);
        if (!has_sell_orders && (_balance[std::get<0>(tpl).first] > threshold)) {
            //std::cout << "В наличии есть " << _balance[std::get<0>(tpl).first] << " "<< std::get<0>(tpl).first << std::endl;
            create_order("sell", symbol, sell_price, sell_quantity, std::get<3>(tpl), std::get<4>(tpl));
            // запоминаем границы
            std::get<1>(tpl) = std::make_pair(min_ask, max_ask);
            has_sell_orders = true;
        }
        if (!has_buy_orders && (_balance[std::get<0>(tpl).second] > 0) && (buy_quantity > std::get<4>(tpl))) {
            std::cout << "В наличии есть " << _balance[std::get<0>(tpl).second] << " "<< std::get<0>(tpl).second << std::endl;
            create_order("buy", symbol, buy_price, buy_quantity, std::get<3>(tpl), std::get<4>(tpl));
            // запоминаем границы
            std::get<2>(tpl) = std::make_pair(min_bid, max_bid);
            has_buy_orders = true;
        }
        // Если есть ордер на продажу, но усреднённое лучшее предложение за пределами удержания — отменить ордер
        if (has_sell_orders && !((std::get<1>(tpl).first < avg_ask) && (avg_ask < std::get<1>(tpl).second))) {
            if (id_sell_order != 0) {
                cancel_order("sell", symbol, id_sell_order);
                has_sell_orders = false;
            }
        }

        // Если есть ордер на покупку, но усреднённое лучшее предложение за пределами удержания — отменить ордер
        if (has_buy_orders && !((std::get<2>(tpl).first < avg_bid) && (avg_bid < std::get<2>(tpl).second))) {
            if (id_buy_order != 0) {
                cancel_order("buy", symbol, id_buy_order);
                has_buy_orders = false;
            }
        }
    }
}

/**
 * Рассчитать среднее арифметическое лучших ордеров для тикера
 *
 * @param ticker Тикер
 * @return Пара, содержащая цену покупки и продажи соответственно
 */
std::pair<dec_float, dec_float> core::avg_orderbooks(const std::string& ticker)
//std::pair<double, double> core::avg_orderbooks(const std::string& ticker)
{
    // Суммы лучших предложений
    dec_float sum_ask(0);
    //double sum_ask(0);
    dec_float sum_bid(0);
    //double sum_bid(0);
    for (auto const&[exchange, exchange_orderbooks]: _orderbooks)
    {
        auto[ask, bid] = exchange_orderbooks.at(ticker);
        sum_ask += ask;
        sum_bid += bid;
    }

    // Количество лучших предложений
    dec_float size(_orderbooks.size());
    //double size(_orderbooks.size());

    // Среднее арифметическое лучших предложений
    dec_float avg_ask(sum_ask / size);
    //double avg_ask(sum_ask / size);
    dec_float avg_bid(sum_bid / size);
    //double avg_bid(sum_bid / size);

    return std::make_pair(avg_ask, avg_bid);
}

/**
 * Создать ордер
 *
 * @param side Тип ордера
 * @param price Цена
 * @param quantity Объём
 */
void core::create_order(std::string_view side_, const std::string& symbol_, const dec_float& price_, const dec_float& quantity_, double price_precission_, double amount_precission_)
{
    //std::cout << boost::lexical_cast<std::string>(price_precission_) << "  " << boost::lexical_cast<std::string>(amount_precission_) << std::endl;
    std::cout << price_ << " " << quantity_<<std::endl;
    std::cout << price_precission_ << " " << amount_precission_<<std::endl;
    //double price = (double)(((int)((price_) / price_precission_))*price_precission_);
    double price = utils::digit_trim(price_.convert_to<double>(), utils::get_exponent(price_precission_));
    //double amount = (double)(((int)((quantity_) / amount_precission_))*amount_precission_);
    double amount = utils::digit_trim(quantity_.convert_to<double>(), utils::get_exponent(amount_precission_));



    JSON create_order_root;
    create_order_root["event"]     = "command";
    create_order_root["exchange"]  = "ftx";
    create_order_root["node"]      = "core";
    create_order_root["instance"]  = 1;
    create_order_root["action"]    = "create_order";
    create_order_root["message"]   = nullptr;
    create_order_root["algo"]      = "spred_bot";
    create_order_root["timestamp"] = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    JSON data;
    for (int i = 0; i < 1; ++i) {
        JSON order;
        order["symbol"]  = symbol_;
        order["type"]    = "limit";
        order["side"]    = side_;
        //order["price"]   = (side_.compare("sell") == 0) ? ceil(price_.convert_to<double>()) : floor(price_.convert_to<double>());
        //order["price"]   = utils::set_number_with_precission(price_precission_, price_.convert_to<std::string>());
        //order["price"]    = boost::lexical_cast<std::string>(price_.convert_to<double>());
        order["price"]  = price;
        //order["amount"]  = utils::set_number_with_precission(_size_precision, quantity_.convert_to<std::string>());
        //order["amount"]  = utils::set_number_with_precission(amount_precission_, quantity_.convert_to<std::string>());
        //order["amount"]  = boost::lexical_cast<std::string>(quantity_.convert_to<double>());
        order["amount"] = amount;
        data.push_back(order);
    }
    create_order_root["data"] = data;

    // логируем информацию
    _orders_logger->info(create_order_root.dump());
    // отправляем команду в гейт
    /*std::int64_t result = _gateway_channel->offer(create_order_root.dump());
    if (result < 0) {
        processing_error("Ошибка отправки команды создания ордера в гейт: ", _prev_command_message, result);
        if (result == -3) {
            result = _gateway_channel->offer(create_order_root.dump());
            if (result < 0) {
                processing_error("Повторная ошибка отправки команды создания ордера в гейт: ", _prev_command_message, result);
            }
        }
    } else {
        // запомним предудущее сообщение
        _prev_command_message = fmt::format("Результат: {}. Сообщение: {}", result, create_order_root.dump());
    }*/
    //metrics_channel->offer(message);
}
/**
 * Отменить ордера
 *
 * @param side Тип ордера
 */
void core::cancel_order(std::string_view side_, std::string symbol_, int64_t id_) {

    JSON cancel_orders;
    cancel_orders["event"]       = "command";
    cancel_orders["exchange"]    = "ftx";
    cancel_orders["node"]        = "core";
    cancel_orders["instance"]    = 1;
    cancel_orders["action"]      = "cancel_order";
    cancel_orders["message"]     = nullptr;
    cancel_orders["algo"]        = "spred_bot";
    cancel_orders["timestamp"]   = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    cancel_orders["data"]        = nullptr;

    JSON data;
    JSON order;
    order["id"]     = std::to_string(id_);
    order["symbol"] = symbol_;
    data.push_back(order);
    /*if (side_ == "sell") {
        for (auto&& sell_order : _sell_orders)
            JSON order;
            order["id"]     = sell_order.first;
            order["symbol"] = sell_order.second;
            data.push_back(order);
        }
    } else if (side_ == "buy") {
        for (auto&& buy_order : _buy_orders) {
            JSON order;
            order["id"]     = buy_order.first;
            order["symbol"] = buy_order.second;
            data.push_back(order);
        }
    }*/

    cancel_orders["data"] = data;
    // логируем информацию
   _orders_logger->info(cancel_orders.dump());
    //std::cout << cancel_orders.dump() << std::endl;
   // отправляем команду в гейт
   /*std::int64_t result = _gateway_channel->offer(cancel_orders.dump());
   if (result < 0) {
       processing_error("Ошибка отправки команды отмены ордеров в гейт: ", _prev_command_message, result);
       if (result == -3) {
           result = _gateway_channel->offer(cancel_orders.dump());
           if (result < 0) {
               processing_error("Повторная ошибка отправки команды отмены ордеров в гейт: ", _prev_command_message, result);
           }
       }
   } else {
       // запомним предудущее сообщение
       _prev_command_message = fmt::format("Результат: {}. Сообщение: {}", result, cancel_orders.dump());
   }*/
}

/**
 * Отменить все ордера
 *
 * @param side Тип ордера
 */
void core::cancel_all_orders(std::string_view side_)
{
    // пока сделаем отмену всех ордеров, так как мы не знаем идентификторов ордеров
     JSON cancel_orders_request;
     cancel_orders_request["event"]       = "command";
     cancel_orders_request["exchange"]    = "ftx";
     cancel_orders_request["node"]        = "core";
     cancel_orders_request["instance"]    = 1;
     cancel_orders_request["action"]      = "cancel_all_orders";
     cancel_orders_request["message"]     = "";
     cancel_orders_request["algo"]        = "spred_bot";
     cancel_orders_request["timestamp"]   = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
     cancel_orders_request["data"]        = nullptr;

     // логируем информацию
    _orders_logger->info(cancel_orders_request.dump());

    std::cout << "" << std::endl;
    // отправляем команду в гейт
    /*std::int64_t result = _gateway_channel->offer(cancel_orders_request.dump());
    if (result < 0) {
        processing_error("Ошибка отправки команды отмены ордера в гейт: ", _prev_command_message, result);
        if (result == -3) {
            result = _gateway_channel->offer(cancel_orders_request.dump());
            if (result < 0) {
                processing_error("Повторная ошибка отправки команды отмены ордера в гейт: ", _prev_command_message, result);
            }
        }
    } else {
        // запомним предудущее сообщение
        _prev_command_message = fmt::format("Результат: {}. Сообщение: {}", result, cancel_orders_request.dump());
    }*/
    //metrics_channel->offer(message);
}
//---------------------------------------------
// возвращает источник конфига
//---------------------------------------------
std::string core::get_config_source()
{
    return _default_config.source;
}
//---------------------------------------------------------------
// посылает ошибку в лог и в консоль
//---------------------------------------------------------------
void core::send_error(std::string_view error_) {
    _general_logger->error(error_);
    _errors_logger->error(error_);
}
//---------------------------------------------------------------
// посылает сообщение в лог и в консоль
//---------------------------------------------------------------
void core::send_message(std::string_view message_){
    _general_logger->info(message_);
}
//---------------------------------------------------------------
// проверяет, получен ли конфиг
//---------------------------------------------------------------
bool core::has_config() {
    return _config_was_received;
}
//---------------------------------------------------------------
// возвращает конфиг с сервера конфигуратора
//---------------------------------------------------------------
bool core::get_config_from_api(bss::error& error_) {
    // делаем get запрос (параметры уже должны быть получены)
    http::response<http::string_body> response = http_session.get_config(_default_config.config_uri, _default_config.config_target);
    return load_config_from_json(response.body(), error_);
}

