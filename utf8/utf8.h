#pragma once

#include <string>
#include <algorithm>
#include <stdexcept>

struct UTF8Url
{
    static std::string Encode(const std::string &url);
    static std::string Decode(const std::string &url);

private:
    static const std::string &HEX_2_NUM_MAP();
    static const std::string &ASCII_EXCEPTION();
    static unsigned char NUM_2_HEX(const char h, const char l);
};