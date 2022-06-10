#include <atomic>
#include <memory>
#include <sentry.h>
#include "core.hpp"
#include "logging.hpp"

//const char* SENTRY_DSN = "https://81fe26996acd4da08ed93398cbd23e91@o1134619.ingest.sentry.io/6182264";
const char* CONFIG_FILE_PATH = "default_config.toml";

std::atomic<bool> running(true);

/**
 * Обработчик прерывания SIGINT
 */
void sigint_handler(int);

int main()
{
    using namespace std::chrono_literals;
    // Инициализация логирования
    init_logging();

    // Инициализация ядра
    std::shared_ptr<core> trade_core = std::make_shared<core>(CONFIG_FILE_PATH);
    bss::error error;
    // получаем источние получения конфига
    std::string source = trade_core->get_config_source();
    // если источник конфига - файл
    if (source.compare("file") == 0) {
        if(std::filesystem::exists("config.json")) {
            if (trade_core->load_config_from_file(error)) {
                trade_core->send_message("Конфигурация загружена из файла.");
            } else {
                trade_core->send_error(error.to_string());
                std::this_thread::sleep_for(1s);
                return EXIT_FAILURE;
            }
        } else {
            error.describe("В параметрах конфигурации указан режим работы с файлом, но файла не существует");
            trade_core->send_error(error.to_string());
            std::this_thread::sleep_for(1s);
            return EXIT_FAILURE;
        }
    } else if (source.compare("agent") == 0) {

    } else if (source.compare("api") == 0) {
        if (trade_core->get_config_from_api(error)) {
            // если конфиг не был получен, то работать нет смысла
            if (!trade_core->has_config()) {
                trade_core->send_error("Файл конфигурации от агента не получен.");
                std::this_thread::sleep_for(1s);
                return EXIT_FAILURE;
            } else {
                trade_core->send_message("Конфигурация получена с сервера.");
            }
        } else {
            trade_core->send_error(error.to_string());
            std::this_thread::sleep_for(1s);
            return EXIT_FAILURE;
        }
    } else {
        trade_core->send_error("Файл конфигурации содержит неизвестный источник получения конфига.");
        std::this_thread::sleep_for(1s);
        return EXIT_FAILURE;
    }
    // продолжаем подготовку к запуску
    if (not trade_core->prepatation_for_launch(error)) {
        return EXIT_FAILURE;
    }
    // Рабочий цикл
    signal(SIGINT, sigint_handler);
    while (running)
        trade_core->poll();

    sentry_close();
    return EXIT_SUCCESS;
}

/**
 * Обработчик прерывания SIGINT
 */
void sigint_handler(int)
{
    running = false;
}
