#pragma once

#define NO_LOGSTREAM

#include <chrono>
#include <cstdint>
#include <string>
#if !defined(NO_LOGSTREAM)
#include <sstream>
#endif

namespace coros {
namespace log {

static const int DATA_MAXSZ = 256;

struct Data {
    uint16_t level;
    uint16_t len;
    uint32_t reserved;
    uint64_t ts;
    char content[DATA_MAXSZ - 2 * sizeof(uint16_t) - sizeof(uint32_t) - sizeof(uint64_t)];
};

enum Level {
    LEVEL_OFF = 0,
    LEVEL_ERROR,
    LEVEL_WARN,
    LEVEL_INFO,
    LEVEL_DEBUG
};

enum OverflowPolicy {
    POLICY_DROP = 0,
    POLICY_ENLARGE_FIFO,
    POLICY_WAIT
};

void open_log(const std::string& file_name, std::size_t fifo_mb, std::size_t roate_mb, OverflowPolicy policy);

void set_level(Level level);

void log_printf(Level level, const char* fmt, ...);

inline uint64_t timestamp_now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

#if !defined(NO_LOGSTREAM)
bool should_log(Level level);

Data* get_data(Level level);

void deliver_log(Data* data);

class LineBuffer
    : public std::streambuf {
public:
    LineBuffer(Data* data);

    uint16_t content_len();

protected:
    virtual std::streambuf::int_type overflow(int_type c = 0);
};

class Line {
public:
    Line(Data* data, uint16_t level);
    ~Line();

    std::ostream& os();

protected:
    Data* data_;
    LineBuffer buffer_;
    std::ostream os_;
};

inline LineBuffer::LineBuffer(Data* data) {
    setp(data->content, data->content + sizeof(data->content));
}

inline uint16_t LineBuffer::content_len() {
    return (uint16_t)(pptr() - pbase());
}

inline std::streambuf::int_type LineBuffer::overflow(int_type c) {
    return 0;
}

inline Line::Line(Data* data, uint16_t level)
    : data_(data), buffer_(data), os_(&buffer_) {
    data->level = level;
    data->ts = timestamp_now();
}

inline std::ostream& Line::os() {
    return os_;
}

inline Line::~Line() {
    data_->len = buffer_.content_len();
    deliver_log(data_);
}
#endif

}
}

#define LOG_OPEN(NAME, BUFMB, ROTATEMB, POLICY) coros::log::open_log(NAME, BUFMB, ROTATEMB, POLICY)
#define LOG_SET_LEVEL(LEVEL) coros::log::set_level(LEVEL)

#if !defined(NO_LOGSTREAM)
#define LOG(LEVEL, logs) \
    do { \
        if (coros::log::should_log(LEVEL)) { \
            coros::log::Data* data__ = coros::log::get_data(LEVEL); \
            if (data__) { \
                coros::log::Line(data__, LEVEL).os() << logs; \
            } \
        } \
    } while (0)

#define LOG_ERROR(logs) LOG(coros::log::LEVEL_ERROR, logs)
#define LOG_WARN(logs)  LOG(coros::log::LEVEL_WARN,  logs)
#define LOG_INFO(logs)  LOG(coros::log::LEVEL_INFO,  logs)
#if defined(NDEBUG)
#define LOG_DEBUG(logs) do {} while (0)
#else
#define LOG_DEBUG(logs) LOG(coros::log::LEVEL_DEBUG, logs)
#endif
#endif

#define log_error(...)  coros::log::log_printf(coros::log::LEVEL_ERROR, __VA_ARGS__)
#define log_warn(...)   coros::log::log_printf(coros::log::LEVEL_WARN,  __VA_ARGS__)
#define log_info(...)   coros::log::log_printf(coros::log::LEVEL_INFO,  __VA_ARGS__)
#if defined(NDEBUG)
#define log_debug(...)  do {} while(0)
#else
#define log_debug(...)  coros::log::log_printf(coros::log::LEVEL_DEBUG, __VA_ARGS__)
#endif
