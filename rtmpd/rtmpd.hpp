#pragma once

#include <cstdint>
#include "coros.hpp"
#include "malog.h"

struct Challenge {
  uint32_t time;
  uint8_t version[4];
  uint8_t randomBytes[1528];
};

static const int RTMP_SIG_SIZE = 1536;

struct Header {
  uint8_t type;
  uint8_t msg_type;
  uint32_t channel;
  uint32_t ts;
  uint32_t len;
  uint32_t stream_id;
  uint64_t final_ts;
};

static const int MAX_CHANNELS = 64;

static const uint8_t HEADER_12_BYTE = 0x00;
static const uint8_t HEADER_8_BYTE = 0x01;
static const uint8_t HEADER_4_BYTE = 0x02;
static const uint8_t HEADER_1_BYTE = 0x03;

inline uint32_t RB32(const uint8_t* x) {
  return (((uint32_t)x[0] << 24) | (x[1] << 16) | (x[2] <<  8) | x[3]);
}

inline uint32_t RL32(const uint8_t* x) {
  return (((uint32_t)x[3] << 24) | (x[2] << 16) | (x[1] <<  8) | x[0]);
}

inline uint32_t RB24(const uint8_t* x) {
  return ((x[0] << 16) | (x[1] <<  8) | x[2]);
}

class Conn {
public:
  bool Start(uv_os_sock_t fd);
  void Fn(uv_os_sock_t fd);
  void ExitFn();

protected:
  bool HandshakeC0(coros::Socket& s);
  bool HandshakeC1(coros::Socket& s);
  bool HandshakeC2(coros::Socket& s);
  void HandlePacket(coros::Socket& s);
  bool ReadHeader(coros::Socket& s, Header& header);

private:
  coros::Coroutine coro_;
  coros::Buffer<4096> rb_;
  coros::Buffer<4096> wb_;
  Header headers_[MAX_CHANNELS];
  uint32_t in_chunk_size_{ 128 };
  uint32_t out_chunk_size_{ 128 };
};

class Listener {
public:
  bool Start();

  void Fn();
  void ExitFn();

protected:
  coros::Coroutine coro_;
};
