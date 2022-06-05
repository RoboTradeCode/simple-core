#include <sstream>
#include <iostream>
#include "utils.hpp"

utils::utils()
{

}
//----------------------------------------------------------------
// получает количество цифр после точки
//----------------------------------------------------------------
uint64_t utils::get_precission(const double& number_)
{
    std::stringstream ss;
    ss << number_;
    //ss.precision(8);
    //ss << std::fixed<< number_;

    std::string number_in_string_ = ss.str();

    size_t exponent_position = number_in_string_.find("e-");
    if (exponent_position != number_in_string_.npos) {
        // есть экспонента
        //std::cout << number_in_string_.substr(exponent_position + 2)<< std::endl;
        //std::cout << std::stoi(number_in_string_.substr(exponent_position + 2)) + 1<< std::endl;
        return std::stoi(number_in_string_.substr(exponent_position + 2)) + 1;
    } else {
        size_t dot_position = number_in_string_.find('.');
        if (dot_position != number_in_string_.npos) {
            return (number_in_string_.size() - 1 - dot_position);
        } else {
            return 0;
        }
    }

}
//----------------------------------------------------------------
//
//----------------------------------------------------------------
uint64_t utils::get_precission(const std::string &value)
{
    std::string number_in_string_ = value;
    size_t dot_position = number_in_string_.find('.');
    if (dot_position != number_in_string_.npos) {
        return (number_in_string_.size() - 1 - dot_position);
    } else {
        return 0;
    }
}
//----------------------------------------------------------------
//
//----------------------------------------------------------------
double utils::get_double(const double &number_) {
    std::stringstream ss;
    ss.precision(8);
    ss << std::fixed<< number_;
    std::string number_in_string_ = ss.str();
    return std::stod(number_in_string_);
}
//----------------------------------------------------------------
//
//----------------------------------------------------------------
int utils::get_count_digits(const double &number_)
{
    std::stringstream ss;
    ss.precision(8);
    ss << std::fixed<< number_;
    std::string number_in_string_ = ss.str();
    size_t dot_position = number_in_string_.find('.');
    if (dot_position != number_in_string_.npos) {
        return (number_in_string_.size() - 1 - dot_position);
    } else {
        return 0;
    }
}
//----------------------------------------------------------------
// устанавливает значение double с учетом precission
//----------------------------------------------------------------
double utils::set_number_with_precission(const uint16_t&  size_precission, std::string number_in_string_)
{
    std::string default_value = number_in_string_;
    size_t dot_position = number_in_string_.find('.');
    if (dot_position != number_in_string_.npos) {
        default_value.erase(dot_position + size_precission + 1);
        //std::cout << default_value.c_str() << std::endl;
    } else {}
    return (dec_float(default_value)).convert_to<double>();
}
//----------------------------------------------------------------
//
//----------------------------------------------------------------
int utils::get_exponent(double step_)
{
    int exponent = 1;
    while(true) {
        if(((step_ * exponent) - (int)(step_ * exponent)) > 0.0) {
            exponent*=10;
        }
        else {
            break;
        }
    }
    return  exponent;
}
//----------------------------------------------------------------
//
//----------------------------------------------------------------
double utils::digit_trim(double value_, int exponent_)
{
    return double((int)(value_ * exponent_)) / exponent_;
}
