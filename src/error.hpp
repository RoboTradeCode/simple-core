#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <system_error>

namespace bss
{

class error final
{
public:
	/**
	* @brief	Конструктор.
	*/
	error() = default;

	/**
	* @brief 	clear Сброс данных описания ошибки.
	*/
	void clear() noexcept {
		// Сбросим код ошибки.
		_error_code.clear();
		// Очистим стек описаний ошибки.
		_stack.clear();
	}

	/**
	* @brief description Получение "системного" кода ошибки.
	*
	* @return	Ссылку на описание ошибки.
	*/
	[[nodiscard]] std::error_code& error_code() noexcept {
		// Вернем код ошибки
		return _error_code;
	}

	/**
	* @brief describe Добавление описания ошибки.
	* @param	description_	Описание ошибки.
	*/
	void describe(std::string_view description_) noexcept {
		// Добавим в стек очередное описание ошибки.
		_stack.emplace_back(description_);
	}

	/**
	* @brief Оператор преобразования к типу bool.
	* @return true	Если была ошибка в процессе работы.
	* @return false	Если работа выполнена без ошибки.
	*/
	[[nodiscard]] operator bool () const noexcept {
		// Если имеется описание или причины, то описание ошибки не пустое.
		return not _stack.empty() || _error_code.operator bool();
	}

	/**
	* @brief empty	Метод проверки на пустое описание ошибки.
	*
	* @return true	Если объект не содержит данных.
	* @return false	Если объект содержит данные описаний.
	*/
	[[nodiscard]] bool empty() const noexcept {
		// Если имеется описание или причины, то описание ошибки не пустое.
		return _stack.empty() && not _error_code.operator bool();
	}

	/**
	* @brief Оператор преобразования к типу std::string.
	*
	* @return Строковое представление ошибки.
	*/
	[[nodiscard]] operator std::string() {
		// Вернем строку.
		return to_string();
	}

	/**
	* @brief to_string Преобразование описания ошибки к std::string.
	*
	* @param add_offset_	Статус добавления отступов к вложенным описаниям ошибки.
	* @param offseter_		Строка добавляемая как отступ для вложенных описаний ошибки.
	* @param splitter_		Строка используемая для разделения уровней описания ошибки.
	*
	* @return Строковое представление ошибки.
	*/
	[[nodiscard]] std::string to_string(
			bool add_offset_ = true,
			std::string_view offseter_ = {" "},
			std::string_view splitter_ = {"\n"}) noexcept {
		// Пока пустая строка.
		std::string result{};
		// Если имеются причины ошибок.
		if (not empty()) {
			// Уровень вывода данных.
			size_t level{0x00};
			// Пройдем все причины ошибки.
			for (auto iter = _stack.rbegin(); iter != _stack.rend(); ++iter) {
				// Добавим очередное описание причины.
				result += sprint(*iter, level++, add_offset_, offseter_, (std::next(iter) != _stack.rend()) ? splitter_ : std::string_view{"", 0x00});
			}
		} else {}
		// Вернем результат работы.
		return result;
	};

private:
	/**
	* @brief _error_code	Экземпляр классического кода ошибки, любезно
	* 	предоставленный нам разработчиками стандартной библиотеки языка C++.
	*/
	std::error_code _error_code;

	/**
	* @brief _stack		Стек описаний ошибки.
	*/
	std::vector<std::string> _stack;

private:
	/**
	* @brief sprint	Выводит данные согласно формата в строку.
	*
	* @param description_	Описание ошибки.
	* @param level_			Уровень вложения.
	* @param add_offset_	Статус добавления начального смещения описания.
	* @param offseter_		Разделитель уровней.
	* @param splitter_		Разделитель строк описаний.
	*
	* @return	Результирующая строка.
	*/
	std::string sprint(
			const std::string& description_, std::size_t level_, bool add_offset_,
			std::string_view offseter_, std::string_view splitter_) const noexcept {
		// Результат работы.
		std::string result;
		// Если не нужно наращивать смещение.
		if (not add_offset_) {
			// Ограничим уровень.
			level_ = 0x00;
		} else {}
		// Выделим сразу место под результирующие данные.
		result.reserve(description_.size() + (level_ * offseter_.size()) + splitter_.size());
		// Если разделитель уровней задан.
		if (not splitter_.empty()) {
			// Вставим необходимое количество разделителей.
			for (size_t cycle = 0x00; cycle < level_; cycle++) {
				// Добавим разделитель.
				result += offseter_;
			}
		} else {}
		// Добавим данные.
		result += const_cast<std::string&>(description_);
		// Если разделитель описаний имеет место быть.
		if (not splitter_.empty()) {
			// Добавим разделитель.
			result += splitter_;
		} else {}
		// Вернем результат работы.
		return result;
	}
};
};

