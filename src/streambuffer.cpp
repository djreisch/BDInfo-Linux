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

// TSStreamBuffer.cs. The C# class reads through a MemoryStream wrapping a
// fixed 5 MB array, so reads past the valid data return stale bytes (and
// 0xFF past the end of the array); bit tests use BitVector32/BitArray, whose
// index arithmetic wraps. All of that is reproduced here.

#include "bdinfo.h"

#include <cstdlib>
#include <cstring>

namespace bdinfo {

namespace {

// BitVector32[mask] with the mask computed as `1 << shift` in C# (the shift
// count is masked to 5 bits).
inline bool BitVector(uint32_t data, int shift)
{
    uint32_t mask = 1u << (static_cast<unsigned>(shift) & 31u);
    return (data & mask) == mask;
}

} // namespace

TSStreamBuffer::TSStreamBuffer()
    : buffer_(static_cast<uint8_t*>(std::calloc(static_cast<size_t>(Capacity), 1)))
{
    if (!buffer_) throw std::bad_alloc();
}

void TSStreamBuffer::Add(const uint8_t* buffer, int offset, int length)
{
    TransferLength += length;

    if (bufferLength_ + length >= Capacity)
    {
        length = static_cast<int>(Capacity) - bufferLength_;
    }

    if (length <= 0) return;

    memcpy(buffer_.get() + bufferLength_, buffer + offset, static_cast<size_t>(length));
    bufferLength_ += length;
}

void TSStreamBuffer::Seek(int64_t offset, bool fromCurrent)
{
    // MemoryStream.Seek
    int64_t target = fromCurrent ? position_ + offset : offset;
    if (target < 0) net::ThrowSeekBeforeBegin();
    position_ = target;
}

void TSStreamBuffer::Reset()
{
    bufferLength_ = 0;
    TransferLength = 0;
}

void TSStreamBuffer::BeginRead()
{
    skipBits_ = 0;
    skippedBytes_ = 0;
    Seek(0, false);
}

int TSStreamBuffer::StreamReadByte()
{
    // MemoryStream.ReadByte: the stream length is the whole array.
    if (position_ >= Capacity) return -1;
    return buffer_.get()[position_++];
}

bool TSStreamBuffer::ReadBytes(int bytes, std::vector<uint8_t>& value)
{
    if (position_ + bytes >= bufferLength_)
    {
        return false;
    }
    value.assign(buffer_.get() + position_, buffer_.get() + position_ + bytes);
    position_ += bytes;
    return true;
}

uint8_t TSStreamBuffer::ReadByte(bool skipH26xEmulationByte)
{
    uint8_t tempByte = static_cast<uint8_t>(StreamReadByte());
    int64_t tempPosition = position_;

    if (skipH26xEmulationByte && tempByte == 0x03)
    {
        Seek(-3, true);
        if (StreamReadByte() == 0x00 && StreamReadByte() == 0x00)
        {
            Seek(1, true);
            tempByte = static_cast<uint8_t>(StreamReadByte());
            skippedBytes_++;
        }
        else
        {
            Seek(tempPosition, false);
        }
    }
    return tempByte;
}

bool TSStreamBuffer::ReadBool(bool skipH26xEmulationByte)
{
    int64_t pos = position_;
    skippedBytes_ = 0;
    if (pos == bufferLength_) return false;

    uint8_t data = ReadByte(skipH26xEmulationByte);
    bool value = BitVector(data, 8 - skipBits_ - 1);

    skipBits_ += 1;
    Seek(pos + (skipBits_ >> 3) + skippedBytes_, false);
    skipBits_ %= 8;

    return value;
}

uint16_t TSStreamBuffer::ReadBits2(int bits, bool skipH26xEmulationByte)
{
    int64_t pos = position_;
    skippedBytes_ = 0;

    int shift = 8;
    uint32_t data = 0;
    for (int i = 0; i < 2; i++)
    {
        if (pos + i >= bufferLength_) break;
        data += static_cast<uint32_t>(ReadByte(skipH26xEmulationByte)) << shift;
        shift -= 8;
    }

    uint16_t value = 0;
    for (int i = skipBits_; i < skipBits_ + bits; i++)
    {
        value = static_cast<uint16_t>(value << 1);
        value = static_cast<uint16_t>(value + (BitVector(data, 16 - i - 1) ? 1 : 0));
    }
    skipBits_ += bits;
    Seek(pos + (skipBits_ >> 3) + skippedBytes_, false);
    skipBits_ %= 8;

    return value;
}

uint32_t TSStreamBuffer::ReadBits4(int bits, bool skipH26xEmulationByte)
{
    int64_t pos = position_;
    skippedBytes_ = 0;

    int shift = 24;
    uint32_t data = 0;
    for (int i = 0; i < 4; i++)
    {
        if (pos + i >= bufferLength_) break;
        data += static_cast<uint32_t>(ReadByte(skipH26xEmulationByte)) << shift;
        shift -= 8;
    }

    uint32_t value = 0;
    for (int i = skipBits_; i < skipBits_ + bits; i++)
    {
        value <<= 1;
        value += BitVector(data, 32 - i - 1) ? 1 : 0;
    }
    skipBits_ += bits;
    Seek(pos + (skipBits_ >> 3) + skippedBytes_, false);
    skipBits_ %= 8;

    return value;
}

uint64_t TSStreamBuffer::ReadBits8(int bits, bool skipH26xEmulationByte)
{
    int64_t pos = position_;
    skippedBytes_ = 0;

    int shift = 24;
    uint32_t data = 0;
    for (int i = 0; i < 4; i++)
    {
        if (pos + i >= bufferLength_) break;
        data += static_cast<uint32_t>(ReadByte(skipH26xEmulationByte)) << shift;
        shift -= 8;
    }

    shift = 24;
    uint32_t data2 = 0;
    for (int i = 0; i < 4; i++)
    {
        if (pos + i >= bufferLength_) break;
        data2 += static_cast<uint32_t>(ReadByte(skipH26xEmulationByte)) << shift;
        shift -= 8;
    }
    // new BitArray(new[] { data2, data }): bits 0-31 from data2, 32-63 from data.
    uint64_t vector = (static_cast<uint64_t>(data) << 32) | data2;

    uint64_t value = 0;
    for (int i = skipBits_; i < skipBits_ + bits; i++)
    {
        int index = 64 - i - 1;
        if (index < 0 || index >= 64) net::ThrowArgumentOutOfRange();
        value <<= 1;
        value += (vector >> index) & 1;
    }

    skipBits_ += bits;
    Seek(pos + (skipBits_ >> 3) + skippedBytes_, false);
    skipBits_ %= 8;

    return value;
}

void TSStreamBuffer::BSSkipBits(int bits, bool skipH26xEmulationByte)
{
    int count = bits / 16 + (bits % 16 > 0 ? 1 : 0);
    int bitsRead = 0;
    for (int i = 0; i < count; i++)
    {
        int bitsToRead = bits - bitsRead;
        bitsToRead = bitsToRead > 16 ? 16 : bitsToRead;
        ReadBits2(bitsToRead, skipH26xEmulationByte);
        bitsRead += bitsToRead;
    }
}

void TSStreamBuffer::BSSkipNextByte()
{
    if (skipBits_ > 0) BSSkipBits(8 - skipBits_);
}

void TSStreamBuffer::BSResetBits() { skipBits_ = 0; }

void TSStreamBuffer::BSSkipBytes(int bytes, bool skipH26xEmulationByte)
{
    if (bytes > 0)
    {
        for (int i = 0; i < bytes; i++) ReadByte(skipH26xEmulationByte);
    }
    else
    {
        int64_t pos = position_;
        Seek(pos + (skipBits_ >> 3) + bytes, false);
    }
}

uint32_t TSStreamBuffer::ReadExp(bool skipH26xEmulationByte)
{
    uint8_t leadingZeroes = 0;
    while (DataBitStreamRemain() > 0 && !ReadBool(skipH26xEmulationByte)) leadingZeroes++;

    double infoD = std::pow(2.0, leadingZeroes);
    uint32_t result = net::ToUInt32(infoD) - 1 + ReadBits4(leadingZeroes, skipH26xEmulationByte);

    return result;
}

void TSStreamBuffer::SkipExp(bool skipH26xEmulationByte)
{
    uint8_t leadingZeroes = 0;
    while (DataBitStreamRemain() > 0 && !ReadBool(skipH26xEmulationByte)) leadingZeroes++;

    BSSkipBits(leadingZeroes, skipH26xEmulationByte);
}

void TSStreamBuffer::SkipExpMulti(int num, bool skipH26xEmulationByte)
{
    for (int i = 0; i < num; i++) SkipExp(skipH26xEmulationByte);
}

int64_t TSStreamBuffer::DataBitStreamRemain() const { return (bufferLength_ - position_) * 8 - skipBits_; }

int64_t TSStreamBuffer::DataBitStreamRemainBytes() const { return bufferLength_ - position_; }

} // namespace bdinfo
