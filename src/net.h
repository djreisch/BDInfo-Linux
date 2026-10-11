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

// Small runtime helpers that reproduce the .NET behaviour the original C#
// code relies on (exception messages, insertion-ordered dictionaries, the
// Array.Sort algorithm, saturating float->int casts, number formatting and
// TimeSpan components), so that reports match the C# build byte for byte.

#pragma once

#include <cstdint>
#include <cmath>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace net {

// ---------------------------------------------------------------------------
// Exceptions (message text matches .NET so user-visible output is identical)
// ---------------------------------------------------------------------------

class Exception : public std::exception
{
public:
    explicit Exception(std::string message) : Message(std::move(message)) {}
    const char* what() const noexcept override { return Message.c_str(); }
    std::string Message;
};

// The exception's message; unlike what(), keeps embedded NUL characters.
std::string MessageOf(const std::exception& ex);

[[noreturn]] void ThrowIndexOutOfRange();
[[noreturn]] void ThrowArgumentOutOfRange();
[[noreturn]] void ThrowNullReference();
[[noreturn]] void ThrowKeyNotFound(const std::string& key);
[[noreturn]] void ThrowSeekBeforeBegin();
[[noreturn]] void ThrowDivideByZero();
[[noreturn]] void ThrowIO(const std::string& message);

// Checked element access for arrays (IndexOutOfRangeException) and
// List<T> (ArgumentOutOfRangeException).
template <class T>
inline T& A(std::vector<T>& v, int64_t index)
{
    if (index < 0 || static_cast<uint64_t>(index) >= v.size()) ThrowIndexOutOfRange();
    return v[static_cast<size_t>(index)];
}
template <class T>
inline const T& A(const std::vector<T>& v, int64_t index)
{
    if (index < 0 || static_cast<uint64_t>(index) >= v.size()) ThrowIndexOutOfRange();
    return v[static_cast<size_t>(index)];
}
template <class T>
inline T& L(std::vector<T>& v, int64_t index)
{
    if (index < 0 || static_cast<uint64_t>(index) >= v.size()) ThrowArgumentOutOfRange();
    return v[static_cast<size_t>(index)];
}
template <class T>
inline const T& L(const std::vector<T>& v, int64_t index)
{
    if (index < 0 || static_cast<uint64_t>(index) >= v.size()) ThrowArgumentOutOfRange();
    return v[static_cast<size_t>(index)];
}

template <class P>
inline P& NN(P& p)
{
    if (!p) ThrowNullReference();
    return p;
}

// ---------------------------------------------------------------------------
// Dictionary<K, V>: enumerates in insertion order, like .NET's Dictionary
// when no entries are removed (the BDInfo code never removes entries).
// ---------------------------------------------------------------------------

std::string KeyToString(uint16_t key);
std::string KeyToString(int key);
std::string KeyToString(double key);
std::string KeyToString(const std::string& key);

template <class K, class V>
class Dictionary
{
public:
    using Entry = std::pair<K, V>;

    bool ContainsKey(const K& key) const { return index_.find(key) != index_.end(); }

    V& operator[](const K& key)
    {
        auto it = index_.find(key);
        if (it == index_.end()) ThrowKeyNotFound(KeyToString(key));
        return entries_[it->second].second;
    }
    const V& operator[](const K& key) const
    {
        auto it = index_.find(key);
        if (it == index_.end()) ThrowKeyNotFound(KeyToString(key));
        return entries_[it->second].second;
    }

    // dict[key] = value
    void Set(const K& key, V value)
    {
        auto it = index_.find(key);
        if (it != index_.end())
        {
            entries_[it->second].second = std::move(value);
            return;
        }
        index_.emplace(key, entries_.size());
        entries_.emplace_back(key, std::move(value));
    }

    // dict.Add(key, value)
    void Add(const K& key, V value)
    {
        if (ContainsKey(key))
            throw Exception("An item with the same key has already been added. Key: " + KeyToString(key));
        Set(key, std::move(value));
    }

    V* TryGet(const K& key)
    {
        auto it = index_.find(key);
        return it == index_.end() ? nullptr : &entries_[it->second].second;
    }

    void Clear()
    {
        entries_.clear();
        index_.clear();
    }

    int Count() const { return static_cast<int>(entries_.size()); }

    typename std::vector<Entry>::iterator begin() { return entries_.begin(); }
    typename std::vector<Entry>::iterator end() { return entries_.end(); }
    typename std::vector<Entry>::const_iterator begin() const { return entries_.begin(); }
    typename std::vector<Entry>::const_iterator end() const { return entries_.end(); }

private:
    struct DoubleHash
    {
        size_t operator()(double d) const { return d == 0.0 ? 0 : std::hash<double>()(d); }
    };
    using Hash = typename std::conditional<std::is_same<K, double>::value, DoubleHash, std::hash<K>>::type;

    std::vector<Entry> entries_;
    std::unordered_map<K, size_t, Hash> index_;
};

// ---------------------------------------------------------------------------
// Array.Sort / List<T>.Sort(Comparison<T>): .NET's introspective sort. It is
// not stable, so reproducing it exactly keeps the order of equal elements.
// ---------------------------------------------------------------------------

namespace detail {

template <class T, class C>
inline void SwapIfGreater(std::vector<T>& k, C& cmp, size_t base, int i, int j)
{
    if (cmp(k[base + i], k[base + j]) > 0) std::swap(k[base + i], k[base + j]);
}

template <class T, class C>
inline void InsertionSort(std::vector<T>& k, C& cmp, size_t base, int length)
{
    for (int i = 0; i < length - 1; i++)
    {
        T t = k[base + i + 1];
        int j = i;
        while (j >= 0 && cmp(t, k[base + j]) < 0)
        {
            k[base + j + 1] = k[base + j];
            j--;
        }
        k[base + j + 1] = t;
    }
}

template <class T, class C>
inline void DownHeap(std::vector<T>& k, C& cmp, size_t base, int i, int n)
{
    T d = k[base + i - 1];
    while (i <= n >> 1)
    {
        int child = 2 * i;
        if (child < n && cmp(k[base + child - 1], k[base + child]) < 0) child++;
        if (!(cmp(d, k[base + child - 1]) < 0)) break;
        k[base + i - 1] = k[base + child - 1];
        i = child;
    }
    k[base + i - 1] = d;
}

template <class T, class C>
inline void HeapSort(std::vector<T>& k, C& cmp, size_t base, int n)
{
    for (int i = n >> 1; i >= 1; i--) DownHeap(k, cmp, base, i, n);
    for (int i = n; i > 1; i--)
    {
        std::swap(k[base], k[base + i - 1]);
        DownHeap(k, cmp, base, 1, i - 1);
    }
}

template <class T, class C>
inline int PickPivotAndPartition(std::vector<T>& k, C& cmp, size_t base, int length)
{
    int hi = length - 1;
    int middle = hi >> 1;
    SwapIfGreater(k, cmp, base, 0, middle);
    SwapIfGreater(k, cmp, base, 0, hi);
    SwapIfGreater(k, cmp, base, middle, hi);

    T pivot = k[base + middle];
    std::swap(k[base + middle], k[base + hi - 1]);
    int left = 0, right = hi - 1;
    while (left < right)
    {
        while (true)
        {
            ++left;
            if (left >= length) ThrowIndexOutOfRange();
            if (!(cmp(k[base + left], pivot) < 0)) break;
        }
        while (true)
        {
            --right;
            if (right < 0) ThrowIndexOutOfRange();
            if (!(cmp(pivot, k[base + right]) < 0)) break;
        }
        if (left >= right) break;
        std::swap(k[base + left], k[base + right]);
    }
    if (left != hi - 1) std::swap(k[base + left], k[base + hi - 1]);
    return left;
}

template <class T, class C>
void IntroSort(std::vector<T>& k, C& cmp, size_t base, int length, int depthLimit)
{
    int partitionSize = length;
    while (partitionSize > 1)
    {
        if (partitionSize <= 16)
        {
            if (partitionSize == 2)
            {
                SwapIfGreater(k, cmp, base, 0, 1);
                return;
            }
            if (partitionSize == 3)
            {
                SwapIfGreater(k, cmp, base, 0, 1);
                SwapIfGreater(k, cmp, base, 0, 2);
                SwapIfGreater(k, cmp, base, 1, 2);
                return;
            }
            InsertionSort(k, cmp, base, partitionSize);
            return;
        }
        if (depthLimit == 0)
        {
            HeapSort(k, cmp, base, partitionSize);
            return;
        }
        depthLimit--;
        int p = PickPivotAndPartition(k, cmp, base, partitionSize);
        IntroSort(k, cmp, base + p + 1, partitionSize - (p + 1), depthLimit);
        partitionSize = p;
    }
}

} // namespace detail

template <class T, class C>
void Sort(std::vector<T>& keys, C cmp)
{
    if (keys.size() > 1)
    {
        uint32_t n = static_cast<uint32_t>(keys.size());
        int log2 = 31 - __builtin_clz(n);
        try
        {
            detail::IntroSort(keys, cmp, 0, static_cast<int>(n), 2 * (log2 + 1));
        }
        catch (const Exception&)
        {
            throw Exception("Failed to compare two elements in the array.");
        }
    }
}

// ---------------------------------------------------------------------------
// Numeric conversions with .NET semantics.
// ---------------------------------------------------------------------------

// (int)/(uint)/(long)/(ulong) casts of double saturate in .NET 9+ (NaN -> 0).
int32_t ToInt32(double v);
uint32_t ToUInt32(double v);
int64_t ToInt64(double v);
uint64_t ToUInt64(double v);

// Math.Round(double): banker's rounding.
inline double Round(double v) { return std::nearbyint(v); }

// ---------------------------------------------------------------------------
// Formatting (InvariantCulture).
// ---------------------------------------------------------------------------

// {0,width}: positive width right-aligns, negative left-aligns.
std::string Pad(const std::string& s, int width);

std::string D(int64_t v, int minDigits = 1);          // {0:D} / {0:D2}
std::string N0(int64_t v);                            // {0:N0} on integers
std::string N0(uint64_t v);
std::string N(double v, int decimals);                // {0:N0} / {0:N2} on double
std::string F(double v, int decimals);                // {0:F2} / ToString("F3")
std::string X(uint64_t v, int minDigits = 1);         // {0:X} / {0:X2}
// Custom formats such as "0", "0.0", "0.0000": the value is first rounded to
// `precision` significant digits (15 for double, 7 for float) and then to
// `decimals` places, rounding half away from zero.
std::string Custom(double v, int decimals, int precision);

std::string ToUpper(std::string s);
std::string ToLower(std::string s);
bool EndsWith(const std::string& s, const std::string& suffix);
bool StartsWith(const std::string& s, const std::string& prefix);
std::string Trim(const std::string& s);
std::string Join(const std::string& separator, const std::vector<std::string>& items);
// string.Compare(a, b) under InvariantGlobalization: ordinal.
int CompareOrdinal(const std::string& a, const std::string& b);

// ---------------------------------------------------------------------------
// TimeSpan components.
// ---------------------------------------------------------------------------

struct TimeSpan
{
    explicit TimeSpan(int64_t ticks) : Ticks(ticks) {}
    static TimeSpan FromSecondsTruncated(double seconds) { return TimeSpan(ToInt64(seconds * 10000000)); }

    int Hours() const { return static_cast<int>((Ticks / 36000000000LL) % 24); }
    int Minutes() const { return static_cast<int>((Ticks / 600000000LL) % 60); }
    int Seconds() const { return static_cast<int>((Ticks / 10000000LL) % 60); }
    int Milliseconds() const { return static_cast<int>((Ticks / 10000LL) % 1000); }
    double TotalHours() const { return static_cast<double>(Ticks) / 36000000000.0; }

    int64_t Ticks;
};

// "{0:D1}:{1:D2}:{2:D2}.{3:D3}" and similar.
std::string FormatTime(const TimeSpan& t, int hourDigits, bool milliseconds);

} // namespace net
