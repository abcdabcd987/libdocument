#pragma once

#include <type_traits>
#include <stdint.h>
#include <cstring>
#include <math.h>
#include <string>
#include <vector>
#include <set>
#include <stdexcept>
#include <assert.h>

constexpr uint32_t BS_MIN_REALLOC = 10;

class BitStream
{
public:
    BitStream() : m_pos(0), m_size(0), m_alloc_size(0), m_data(nullptr), m_read_only(false)
    {
    }

    /**
     * @brief BitStream
     * @param data
     * @param length
     *
     * NOTE: Use assign() if you want to prevent memcpy
     */
    BitStream(const uint8_t* data, const uint32_t length)
        : m_pos(0), m_size(0), m_alloc_size(BS_MIN_REALLOC), m_data(nullptr), m_read_only(false)
    {
        m_data = reinterpret_cast<uint8_t*>(malloc(m_alloc_size));
        write_raw_data(data, length, false);
    }

    BitStream(BitStream &&other)
        : m_pos(other.m_pos), m_size(other.m_size), m_alloc_size(other.m_alloc_size), m_data(other.m_data), m_read_only(other.m_read_only)
    {
        other.m_data = nullptr;
    }

    BitStream(const BitStream &other) = delete;

    ~BitStream()
    {
        clear();
    }

    void pre_alloc(uint32_t size)
    {
        if(size < m_alloc_size)
            throw std::runtime_error("Can only increase alloc size");

        m_alloc_size = size;
        m_data = reinterpret_cast<uint8_t*>(realloc(m_data, m_alloc_size));

        if(m_data == nullptr)
            throw std::runtime_error("Failed to allocated data");
    }

    void copy(const BitStream &other, bool ignore_read_only = false)
    {
        clear();

        if(other.m_read_only && !ignore_read_only)
        {
            resize(0);
            m_read_only = true;
            m_data = other.m_data;
            m_size = m_alloc_size = other.m_size;
        }
        else
        {
            m_read_only = false;
            resize(0);
            move_to(0);
            write_raw_data(other.data(), other.size());
            move_to(0);
        }
    }

    void clear()
    {
        if(m_data != nullptr && !m_read_only)
        {
            free(m_data);
        }

        m_read_only = false;
        m_data = nullptr;
        m_size = m_alloc_size = 0;
    }

    BitStream duplicate() const
    {
        return BitStream(m_data, m_size);
    }

    void operator=(BitStream &&other)
    {
        m_data = other.m_data;
        m_read_only = other.m_read_only;
        m_pos = other.m_pos;
        m_size = other.m_size;
        m_alloc_size = other.m_alloc_size;

        other.m_data = nullptr;
        other.m_pos = other.m_size = other.m_alloc_size = 0;
    }

    void resize(uint32_t new_size)
    {
        if(m_read_only)
            throw std::runtime_error("Cannot resize: is read-only!");

        if(m_data == nullptr)
        {
            m_size = m_alloc_size = new_size;
            if(new_size > 0)
                m_data = reinterpret_cast<uint8_t*>(malloc(new_size));
            return;
        }

        if(new_size < m_size)
        {
            assert(new_size < m_alloc_size);
            m_size = new_size;
            //no reason to change alloc size?
            return;
        }

        if(new_size <= m_alloc_size)
        {
            m_size = new_size;
            return;
        }

        assert(new_size > m_alloc_size);
        assert(new_size > m_size);

        uint32_t step = std::max(BS_MIN_REALLOC, new_size - m_size);
        pre_alloc(m_alloc_size+step);

        m_size = new_size;
        assert(m_size <= m_alloc_size);
    }

    BitStream& write_raw_data(const uint8_t* data, const uint32_t length, bool advance = true)
    {
        if (m_pos + length > size())
            resize(m_pos + length);

        memcpy(&m_data[m_pos], data, length);

        if(advance)
            m_pos += length;

        return *this;
    }

    void make_space(uint32_t increase)
    {
        if(m_read_only)
            throw std::runtime_error("Cannot make space in read only mode");

        auto remain = remaining_size();
        resize(size() + increase);
        memmove(current()+increase, current(), remain);
    }

    void remove_space(uint32_t decrease)
    {
        if(decrease > remaining_size())
            throw std::runtime_error("Not enoug space left");

        memmove(current(), current()+decrease, remaining_size()-decrease);
        resize(size() - decrease);
    }

    bool read_raw_data(uint8_t **data, uint32_t length)
    {
        if(m_pos+length > size())
           return false;
           //throw std::runtime_error("Cannot read from BitStream. Already at the end.");

        *data = &m_data[m_pos];
        m_pos += length;
        return true;
    }

    template<typename T>
    BitStream& operator<<(const std::set<T>& data);

    template<typename T>
    BitStream& operator<<(const std::vector<T>& data);

    template<typename T>
    BitStream& operator>>(std::vector<T>& data);

    template<typename T>
    BitStream& operator>>(std::set<T>& set);

    /**
     * Write virtually any kind of data to the stream
     *
     * @note data is expected not to be a pointer. you might have to dereference it
     */

    template<typename T>    
    BitStream& operator<<(const T& data)
    {
        static_assert(std::is_pod<T>());

        if(m_pos + sizeof(T) >= size())
        {
            uint32_t newSize = m_pos + sizeof(T);
            resize(newSize);
        }

        memcpy(&m_data[m_pos], &data, sizeof(T));
        m_pos += sizeof(T);

        return *this;
    }

    /**
     * Read virtually any kind of data from the stream
     *
     * @note data is expected not to be a pointer. you might have to dereference it
     */
    template<typename T>
    BitStream& operator>>(T& data)
    {
        if(m_pos+sizeof(T) > size())
           throw std::runtime_error("Cannot read from BitStream: Already at the end.");

        memcpy(&data, &m_data[m_pos], sizeof(T));
        m_pos += sizeof(T);

        return *this;
    }

    /**
     * @brief Access raw content of the stream
     * @note This should only be used internally to write to the socket
     */
    const uint8_t* data() const
    {
        return m_data;
    }

    uint8_t* data()
    {
        return m_data;
    }

    void detach(uint8_t*& out, uint32_t &len)
    {
        if(m_size < m_alloc_size)
        {
            out = reinterpret_cast<uint8_t*>(realloc(m_data, m_size));
        }
        else
        {
            assert(m_size == m_alloc_size);
            out = m_data;
        }

        len = m_size;

        m_data = nullptr;
        m_size = 0;
        m_alloc_size = 0;
    }

    int64_t hash() const
    {
        int64_t result = 0;
        uint8_t *ptr = reinterpret_cast<uint8_t*>(&result);

        for(uint32_t i = 0; i < this->size(); ++i)
        {
            ptr[i % 8] = ptr[i % 8] ^ m_data[i];
        }

        return result;
    }

    void assign(const uint8_t *buffer, uint32_t len, bool read_only)
    {
        if(read_only == false)
            throw std::runtime_error("Cannot use const reference and writable buffer");

        assign(const_cast<uint8_t*>(buffer), len, true);
    }

    void assign(uint8_t* buffer, uint32_t len, bool read_only = false)
    {
        if(m_data != nullptr)
        {
            assert(!m_read_only);
            delete []m_data;
        }

        m_data = buffer;
        m_size = m_alloc_size = len;
        m_pos = 0;
        m_read_only = read_only;
    }

    /**
     * @brief Return the current size of the stream
     */
    size_t size() const
    {
        return m_size;
    }

    bool empty() const
    {
        return m_size == 0;
    }

    size_t remaining_size() const
    {
        return size() - pos();
    }

    size_t allocated_size() const
    {
        return m_alloc_size;
    }

    /**
     * @brief Did we read until the end of the stream
     * @note This only makes sense when reading. Writing dynamically expands the stream
     */
    bool at_end() const
    {
        return m_pos == m_size;
    }

    /**
     * @brief Get the currenty position
     * @return Returns the position (in Bytes)
     */
    uint32_t pos() const
    {
        return m_pos;
    }

    bool move_to(uint32_t pos, bool allocate = false)
    {
        if(pos > m_size)
        {
            if(allocate)
                resize(pos+1);
            else
                return false;
        }

         m_pos = pos;
         return true;
    }

    bool move_by(int32_t offset, bool allocate = false)
    {
        // make sure we don't overflow
        if(offset < 0 && static_cast<uint32_t>((-1)*offset) > m_pos)
            throw std::runtime_error("Can't move. Would overflow buffer!");

        return move_to(m_pos + offset, allocate);
    }

    /**
     * @brief Get raw pointer to the current position in the stream
     * @return
     */
    uint8_t* current()
    {
        return &m_data[m_pos];
    }

    const uint8_t* current() const
    {
        return &m_data[m_pos];
    }

private:
    uint32_t m_pos;
    uint32_t m_size, m_alloc_size;

    uint8_t *m_data;

    // a read only bitstream cannot modify the underlying data
    // useful to avoid copying and delete
    bool m_read_only;
};

template<typename T> inline
BitStream& BitStream::operator>>(std::set<T> &data)
{
    uint32_t size = 0;
    (*this) >> size;

    for(uint32_t i = 0; i < size; ++i)
    {
        T t;
        (*this) >> t;
        data.insert(t);
    }

    return (*this);
}

template<typename T> inline
BitStream& BitStream::operator>>(std::vector<T> &data)
{
    uint32_t size = 0;
    (*this) >> size;
    data.resize(size);

    for(uint32_t i = 0; i < size; ++i)
    {
        (*this) >> data[i];
    }
    return (*this);
}

template<typename T> inline
BitStream& BitStream::operator<<(const std::vector<T>& data)
{
    uint32_t size = data.size();
    (*this) << size;

    for(uint32_t i = 0; i < data.size(); ++i)
    {
        (*this) << data[i];
    }

    return *this;
}

template<typename T> inline
BitStream& BitStream::operator<< (const std::set<T> &s)
{
    uint32_t size = s.size();
    (*this) << size;

    for(auto &e: s)
    {
        (*this) << e;
    }

    return *this;
}

template<> inline
BitStream& BitStream::operator<< <std::string>(const std::string& str)
{
    uint32_t length = str.size();
    *this << length;

    write_raw_data(reinterpret_cast<const uint8_t*>(str.c_str()), length);

    return *this;
}

template<> inline
BitStream& BitStream::operator<< <BitStream>(const BitStream &other)
{
    uint32_t length = other.size();
    *this << length;

    write_raw_data(other.data(), length);
    return *this;
}

template<> inline
BitStream& BitStream::operator>> <BitStream>(BitStream &bstream)
{
    if(bstream.size() > 0)
        throw std::runtime_error("Target bstream already contains data");

    uint32_t length = 0;
    *this >> length;

    bstream.write_raw_data(current(), length);
    move_by(length);
    bstream.move_to(0);

    return *this;
}

template<> inline
BitStream& BitStream::operator>> <char>(char& data)
{
    if(at_end())
        throw std::runtime_error("Cannot read more: Already at end");

    data = m_data[m_pos];
    m_pos++;

    return *this;
}

template<> inline
BitStream& BitStream::operator>> <std::string>(std::string& data)
{
    data = "";

    uint32_t length;
    *this >> length;

    for(uint32_t i = 0; i < length; ++i)
    {
        char c;
        *this >> c;

        data += c;
    }

    return *this;
}

inline bool operator==(const BitStream &first, const BitStream &second)
{
    if(first.size() != second.size())
        return false;
    else
        return memcmp(first.data(), second.data(), first.size()) == 0;
}

 
