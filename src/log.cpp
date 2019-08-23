#include "log.hpp"
#include <atomic>
#include <memory>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <iostream>

namespace coros {
namespace log {
namespace impl {

class DataPool {
public:
    DataPool(std::size_t limit_mb, OverflowPolicy policy);

    void reclaim();
    Data* get(Level level);
    void put(Data* data);

protected:
    std::mutex pool_lock_;
    std::vector<Data* > pool_;
    std::condition_variable pool_cond_;
    OverflowPolicy policy_;
    std::vector<Data* > heads_;
};

class FileWriter {
public:
    FileWriter(const std::string& file_name, std::size_t rotate_mb);

    void open();
    void write(Data* data);
    void flush();
    void close();

protected:
    void rotate();

protected:
    std::string file_name_;
    std::size_t rotate_size_;
    FILE* fp_{ nullptr };
    std::size_t current_size_{ 0 };
};

class Logger {
public:
    Logger(const std::string& file_name, std::size_t limit_mb, std::size_t rotate_mb, OverflowPolicy policy);
    ~Logger();

    Data* get(Level level);
    void put(Data* data);

protected:
    void run();
    void consume(std::vector<Data* >& items);

protected:
    bool shut_{ false };
    std::mutex write_lock_;
    std::condition_variable write_cond_;
    std::vector<Data* > items_;
    std::thread write_thread_;
    DataPool pool_;
    FileWriter writer_;
};

inline Data* DataPool::get(Level level) {
    std::unique_lock<std::mutex> l(pool_lock_);
    if (pool_.size() > 0) {
        Data* data = pool_.back();
        pool_.pop_back();
        return data;
    }
    if (policy_ == POLICY_DROP) {
        return nullptr;
    } else if (policy_ == POLICY_ENLARGE_FIFO) {
        return new Data();
    } else {
        pool_cond_.wait(l);
        if (pool_.size() > 0) {
            Data* data = pool_.back();
            pool_.pop_back();
            return data;
        } else {
            return nullptr;
        }
    }
}

inline void DataPool::put(Data* data) {
    std::lock_guard<std::mutex> l(pool_lock_);
    pool_.push_back(data);
    pool_cond_.notify_all();
}

inline void FileWriter::flush() {
    fflush(fp_);
}

inline Data* Logger::get(Level level) {
    return pool_.get(level);
}

inline void Logger::put(Data* data) {
    std::lock_guard<std::mutex> l(write_lock_);
    items_.push_back(data);
    write_cond_.notify_all();
}

}

std::unique_ptr<impl::Logger> logger;
std::atomic<uint16_t> threshold = { LEVEL_DEBUG };

void open_log(const std::string& file_name, std::size_t buf_mb, std::size_t rotate_mb, OverflowPolicy policy) {
    logger.reset(new impl::Logger(file_name, buf_mb, rotate_mb, policy));
}

Data* get_data(Level level) {
    return logger->get(level);
}

bool should_log(Level level) {
    return static_cast<uint16_t>(level) <= threshold.load();
}

void set_level(Level level) {
    threshold.store(static_cast<uint16_t>(level));
}

void deliver_log(Data* data) {
    logger->put(data);
}

void log_printf(Level level, const char* fmt, ...) {
    if (should_log(level)) {
        Data* data = get_data(level);
        if (data) {
            data->level = static_cast<uint16_t>(level);
            data->ts = timestamp_now();
            va_list args;
            va_start(args, fmt);
            int len = vsnprintf(data->content, sizeof(data->content), fmt, args);
            va_end(args);
            if (len < 0) {
                data->len = 0;
            } else {
                data->len = static_cast<uint16_t>(len);
                if (data->len > sizeof(data->content)) {
                    data->len = sizeof(data->content);
                }
            }
            deliver_log(data);
        }
    }
}

namespace impl {

static const int level_tag_len = 8;
static const char* level_tag[] = {
    "[OFF  ] ",
    "[ERROR] ",
    "[WARN ] ",
    "[INFO ] ",
    "[DEBUG] "
};

DataPool::DataPool(std::size_t limit_mb, OverflowPolicy policy)
    : policy_(policy) {
    std::size_t limit_items = limit_mb * 1024 * 1024 / DATA_MAXSZ;
    Data* head = new Data[limit_items];
    heads_.push_back(head);
    Data* next = head;
    for (std::size_t i = 0; i < limit_items; i++) {
        pool_.push_back(next);
        next ++;
    }
}

void DataPool::reclaim() {
    pool_.clear();
    for (auto i = heads_.begin(); i != heads_.end(); i++) {
        delete (*i);
    }
    heads_.clear();
}

FileWriter::FileWriter(const std::string& file_name, std::size_t rotate_mb)
    : file_name_(file_name), rotate_size_(rotate_mb * 1024 * 1024) {
}

void FileWriter::open() {
    if (file_name_ != "@stdout") {
        fp_ = fopen(file_name_.c_str(), "ab");
        if (fp_) {
            int fd = fileno(fp_);
            struct stat st;
            if (::fstat(fd, &st) == 0) {
                current_size_ = static_cast<size_t>(st.st_size);
            } else {
                current_size_ = 0;
            }
        }
    }
    if (!fp_) {
        fp_ = stdout;
        current_size_ = 0;
        rotate_size_ = 0;
    }
}

void FileWriter::write(Data* data) {
    std::time_t time_t = data->ts / 1000000;
    auto tm = std::localtime(&time_t);
    int len = fprintf(fp_, "[%04d-%02d-%02d %02d:%02d:%02d.%06d] %.*s%.*s\n",
                      tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                      tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(data->ts % 1000000),
                      level_tag_len, level_tag[data->level], (int)data->len, data->content);
    if (len > 0) {
        current_size_ += len;
    }
    if (data->level == LEVEL_ERROR) {
        fflush(fp_);
    }
    if (rotate_size_ && current_size_ >= rotate_size_) {
        rotate();
    }
}

void FileWriter::close() {
    if (fp_ && fp_ != stdout) {
        fclose(fp_);
    }
    fp_ = nullptr;
}

void FileWriter::rotate() {
    close();

    std::time_t time_t = timestamp_now() / 1000000;
    auto tm = std::localtime(&time_t);
    char newpath[2048];
    snprintf(newpath, 2048, "%s.%04d%02d%02d-%02d%02d%02d.%06d",
             file_name_.c_str(),
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec,
             (int)(time_t % 1000000));
    if (::rename(file_name_.c_str(), newpath) == -1) {
        std::cerr << "cannot rename: " << file_name_ << " to " << newpath << std::endl;
    }

    open();
    current_size_ = 0;
}

Logger::Logger(const std::string& file_name, std::size_t limit_mb, std::size_t rotate_mb, OverflowPolicy policy)
    : pool_(limit_mb, policy), writer_(file_name, rotate_mb) {
    write_thread_ = std::thread(&Logger::run, this);
}

Logger::~Logger() {
    {
        std::lock_guard<std::mutex> l(write_lock_);
        shut_ = true;
        write_cond_.notify_all();
    }
    write_thread_.join();
    pool_.reclaim();
}

void Logger::run() {
    writer_.open();

    std::vector<Data* > items;
    while (true) {
        {
            std::unique_lock<std::mutex> l(write_lock_);
            if (shut_) {
                break;
            }
            if (items_.empty()) {
                writer_.flush();
                write_cond_.wait(l);
            }
            std::swap(items, items_);
        }
        consume(items);
    }
    consume(items_);

    writer_.close();
}

void Logger::consume(std::vector<Data* >& items) {
    for (auto i = items.begin(); i != items.end(); i++) {
        writer_.write((*i));
        pool_.put((*i));
    }
    items.clear();
}

}
}
}
