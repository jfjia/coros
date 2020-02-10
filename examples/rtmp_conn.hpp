#pragma once

#include <cstdint>
#include "coros.hpp"

static const int RTMP_SIG_SIZE = 1536;

static const int MAX_CHANNELS = 64;

static const uint8_t HEADER_12_BYTE = 0x00;
static const uint8_t HEADER_8_BYTE = 0x01;
static const uint8_t HEADER_4_BYTE = 0x02;
static const uint8_t HEADER_1_BYTE = 0x03;

struct Challenge {
  uint32_t time;
  uint8_t version[4];
  uint8_t randomBytes[1528];
};

struct Header {
  uint8_t type;
  uint8_t msg_type;
  uint32_t channel;
  uint32_t ts;
  uint32_t len;
  uint32_t stream_id;
  uint64_t final_ts;
};

struct IoBuf {
  coros::Buffer<4096> i;
  coros::Buffer<4096> o;

  IoBuf(coros::Socket& s) : i(s), o(s) {
  }
};

class Conn {
public:
  enum class Type {
    HOST, CLIENT
  };

  bool Start(uv_os_sock_t fd);
  void Fn(uv_os_sock_t fd);
  void ExitFn();

protected:
  bool Handshake(IoBuf& io);
  bool ReadHeader(IoBuf& io, Header& header);

private:
  coros::Coroutine coro_;
  Header headers_[MAX_CHANNELS];
  uint32_t in_chunk_size_{ 128 };
  uint32_t out_chunk_size_{ 128 };
  Type type_{ Type::HOST };
};
