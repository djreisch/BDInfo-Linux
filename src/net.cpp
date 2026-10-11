//============================================================================
// BDInfo - Blu-ray Video and Audio Analysis Tool
// Copyright © 2010 Cinema Squid
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//=============================================================================

#include "net.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace net {

std::string MessageOf(const std::exception& ex)
{
    if (auto* e = dynamic_cast<const Exception*>(&ex)) return e->Message;
    return ex.what();
}

void ThrowIndexOutOfRange() { throw Exception("Index was outside the bounds of the array."); }

void ThrowArgumentOutOfRange()
{
    throw Exception("Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')");
}

void ThrowNullReference() { throw Exception("Object reference not set to an instance of an object."); }

void ThrowKeyNotFound(const std::string& key)
{
    throw Exception("The given key '" + key + "' was not present in the dictionary.");
}

void ThrowSeekBeforeBegin() { throw Exception("An attempt was made to move the position before the beginning of the stream."); }

void ThrowDivideByZero() { throw Exception("Attempted to divide by zero."); }

void ThrowIO(const std::string& message) { throw Exception(message); }

std::string KeyToString(uint16_t key) { return std::to_string(key); }
std::string KeyToString(int key) { return std::to_string(key); }
std::string KeyToString(const std::string& key) { return key; }

std::string KeyToString(double key)
{
    // double.ToString(): shortest round-trippable representation.
    char buf[64];
    for (int precision = 1; precision <= 17; precision++)
    {
        snprintf(buf, sizeof buf, "%.*g", precision, key);
        if (strtod(buf, nullptr) == key) break;
    }
    return buf;
}

// ---------------------------------------------------------------------------

int32_t ToInt32(double v)
{
    if (std::isnan(v)) return 0;
    if (v >= 2147483648.0) return INT32_MAX;
    if (v <= -2147483649.0) return INT32_MIN;
    return static_cast<int32_t>(v);
}

uint32_t ToUInt32(double v)
{
    if (std::isnan(v) || v <= -1.0) return 0;
    if (v >= 4294967296.0) return UINT32_MAX;
    return static_cast<uint32_t>(v);
}

int64_t ToInt64(double v)
{
    if (std::isnan(v)) return 0;
    if (v >= 9223372036854775808.0) return INT64_MAX;
    if (v < -9223372036854775808.0) return INT64_MIN;
    return static_cast<int64_t>(v);
}

uint64_t ToUInt64(double v)
{
    if (std::isnan(v) || v <= -1.0) return 0;
    if (v >= 18446744073709551616.0) return UINT64_MAX;
    return static_cast<uint64_t>(v);
}

// ---------------------------------------------------------------------------

// Length in UTF-16 code units, which is what .NET pads against.
static int Utf16Length(const std::string& s)
{
    int length = 0;
    for (unsigned char c : s)
    {
        if ((c & 0xC0) != 0x80) length++;
        if (c >= 0xF0) length++; // 4-byte sequences become surrogate pairs
    }
    return length;
}

std::string Pad(const std::string& s, int width)
{
    int length = Utf16Length(s);
    int target = width < 0 ? -width : width;
    if (length >= target) return s;
    std::string padding(static_cast<size_t>(target - length), ' ');
    return width < 0 ? s + padding : padding + s;
}

std::string D(int64_t v, int minDigits)
{
    uint64_t magnitude = v < 0 ? 0 - static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
    std::string digits = std::to_string(magnitude);
    if (static_cast<int>(digits.size()) < minDigits)
        digits.insert(0, static_cast<size_t>(minDigits) - digits.size(), '0');
    return v < 0 ? "-" + digits : digits;
}

static std::string Group(const std::string& integerDigits)
{
    std::string out;
    int count = 0;
    for (size_t i = integerDigits.size(); i-- > 0;)
    {
        out.insert(out.begin(), integerDigits[i]);
        if (++count % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return out;
}

std::string N0(int64_t v)
{
    uint64_t magnitude = v < 0 ? 0 - static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
    std::string grouped = Group(std::to_string(magnitude));
    return v < 0 ? "-" + grouped : grouped;
}

std::string N0(uint64_t v) { return Group(std::to_string(v)); }

std::string F(double v, int decimals)
{
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "\xE2\x88\x9E" : "-\xE2\x88\x9E";
    // .NET Core 3.0+ formats the exact binary value, rounding ties to even,
    // exactly like glibc's printf.
    std::vector<char> buf(400);
    int n = snprintf(buf.data(), buf.size(), "%.*f", decimals, v);
    if (n >= static_cast<int>(buf.size()))
    {
        buf.resize(static_cast<size_t>(n) + 1);
        snprintf(buf.data(), buf.size(), "%.*f", decimals, v);
    }
    return buf.data();
}

std::string N(double v, int decimals)
{
    std::string fixed = F(v, decimals);
    if (fixed == "NaN" || fixed.find("\xE2\x88\x9E") != std::string::npos) return fixed;
    bool negative = !fixed.empty() && fixed[0] == '-';
    if (negative) fixed.erase(0, 1);
    size_t dot = fixed.find('.');
    std::string integer = dot == std::string::npos ? fixed : fixed.substr(0, dot);
    std::string fraction = dot == std::string::npos ? "" : fixed.substr(dot);
    return (negative ? "-" : "") + Group(integer) + fraction;
}

std::string X(uint64_t v, int minDigits)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%llX", static_cast<unsigned long long>(v));
    std::string s = buf;
    if (static_cast<int>(s.size()) < minDigits) s.insert(0, static_cast<size_t>(minDigits) - s.size(), '0');
    return s;
}

std::string Custom(double v, int decimals, int precision)
{
    bool negative = std::signbit(v);
    double magnitude = std::fabs(v);

    // Digit buffer of `precision` significant digits, as .NET builds it.
    std::string digits;
    int scale = 0; // number of digits before the decimal point
    if (magnitude != 0)
    {
        char buf[64];
        snprintf(buf, sizeof buf, "%.*e", precision - 1, magnitude);
        const char* e = strchr(buf, 'e');
        for (const char* p = buf; p < e; p++)
            if (*p != '.') digits.push_back(*p);
        scale = atoi(e + 1) + 1;
        while (!digits.empty() && digits.back() == '0') digits.pop_back();
    }

    // Number.RoundNumber: round half up at the requested position.
    int pos = scale + decimals;
    int i = 0;
    while (i < pos && i < static_cast<int>(digits.size())) i++;
    if (i == pos && i < static_cast<int>(digits.size()) && digits[static_cast<size_t>(i)] >= '5')
    {
        while (i > 0 && digits[static_cast<size_t>(i) - 1] == '9') i--;
        if (i > 0)
        {
            digits[static_cast<size_t>(i) - 1]++;
        }
        else
        {
            scale++;
            digits = "1";
            i = 1;
        }
    }
    else
    {
        while (i > 0 && digits[static_cast<size_t>(i) - 1] == '0') i--;
    }
    digits.resize(static_cast<size_t>(i));
    if (i == 0) scale = 0;

    auto digitAt = [&](int index) -> char {
        return index >= 0 && index < static_cast<int>(digits.size()) ? digits[static_cast<size_t>(index)] : '0';
    };

    std::string out;
    if (scale <= 0)
    {
        out = "0";
    }
    else
    {
        for (int k = 0; k < scale; k++) out.push_back(digitAt(k));
    }
    if (decimals > 0)
    {
        out.push_back('.');
        for (int k = 0; k < decimals; k++) out.push_back(digitAt(scale + k));
    }
    if (negative && !digits.empty()) out.insert(out.begin(), '-');
    return out;
}

std::string ToUpper(std::string s)
{
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

std::string ToLower(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool EndsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool StartsWith(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string Trim(const std::string& s)
{
    const char* ws = " \t\n\v\f\r";
    size_t begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) return "";
    size_t end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

std::string Join(const std::string& separator, const std::vector<std::string>& items)
{
    std::string out;
    for (size_t i = 0; i < items.size(); i++)
    {
        if (i > 0) out += separator;
        out += items[i];
    }
    return out;
}

int CompareOrdinal(const std::string& a, const std::string& b)
{
    int c = a.compare(b);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

std::string FormatTime(const TimeSpan& t, int hourDigits, bool milliseconds)
{
    std::string s = D(t.Hours(), hourDigits) + ":" + D(t.Minutes(), 2) + ":" + D(t.Seconds(), 2);
    if (milliseconds) s += "." + D(t.Milliseconds(), 3);
    return s;
}

} // namespace net
