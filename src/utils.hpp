#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <boost/multiprecision/cpp_dec_float.hpp>

using dec_float = boost::multiprecision::cpp_dec_float_50;

class utils
{
public:
    utils();

    // получает количество цифр после точки
    static uint64_t get_precission(const double& value);
    static uint64_t get_precission(const std::string& value);
    static double get_double(const double& value);
    static int    get_count_digits(const double& value);
    // устанавливает значение double с учетом precission
    static double set_number_with_precission(const uint16_t&  size_precision, std::string string_number_);

    static int get_exponent(double step_);
    static double digit_trim(double value_, int exponent_);
};

#endif // UTILS_H
