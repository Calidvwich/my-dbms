#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "errors.h"

inline bool is_leap_year(int year) {
    return year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
}

inline int days_in_month(int year, int month) {
    static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month];
}

inline int64_t parse_datetime(const std::string &text) {
    if (text.size() != 19 || text[4] != '-' || text[7] != '-' || text[10] != ' ' ||
        text[13] != ':' || text[16] != ':') {
        throw InvalidDatetimeError(text);
    }
    for (size_t i = 0; i < text.size(); i++) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) {
            continue;
        }
        if (text[i] < '0' || text[i] > '9') {
            throw InvalidDatetimeError(text);
        }
    }

    int year = std::stoi(text.substr(0, 4));
    int month = std::stoi(text.substr(5, 2));
    int day = std::stoi(text.substr(8, 2));
    int hour = std::stoi(text.substr(11, 2));
    int minute = std::stoi(text.substr(14, 2));
    int second = std::stoi(text.substr(17, 2));

    if (year < 1000 || year > 9999 || month < 1 || month > 12 ||
        day < 1 || day > days_in_month(year, month) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        throw InvalidDatetimeError(text);
    }

    int64_t encoded = year;
    encoded = encoded * 100 + month;
    encoded = encoded * 100 + day;
    encoded = encoded * 100 + hour;
    encoded = encoded * 100 + minute;
    encoded = encoded * 100 + second;
    return encoded;
}

inline std::string format_datetime(int64_t encoded) {
    int second = encoded % 100;
    encoded /= 100;
    int minute = encoded % 100;
    encoded /= 100;
    int hour = encoded % 100;
    encoded /= 100;
    int day = encoded % 100;
    encoded /= 100;
    int month = encoded % 100;
    encoded /= 100;
    int year = static_cast<int>(encoded);

    char buffer[20];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                  year, month, day, hour, minute, second);
    return buffer;
}
