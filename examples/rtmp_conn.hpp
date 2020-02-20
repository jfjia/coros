#pragma once

#include <cstdint>
#include "coros.hpp"
#include "rtmp_amf.hpp"

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

class IoBuf {
protected:
  coros::Buffer<4096> i;
  coros::Buffer<4096> o;

public:
  IoBuf(coros::Socket& s) : i(s), o(s) {
  }

  bool Read8(uint8_t& val) {
    if (!i.Read(1)) {
      return false;
    }
    val = *((uint8_t*)i.Data());
    i.RemoveConsumed(1);
    return true;
  }

  bool ReadBE24(uint32_t& val) {
    if (!i.Read(3)) {
      return false;
    }
    val = RB24((const uint8_t*)i.Data());
    i.RemoveConsumed(3);
    return true;
  }

  bool ReadLE32(uint32_t& val) {
    if (!i.Read(4)) {
      return false;
    }
    val = RL32((const uint8_t*)i.Data());
    i.RemoveConsumed(4);
    return true;
  }

  bool ReadBE32(uint32_t& val) {
    if (!i.Read(4)) {
      return false;
    }
    val = RB32((const uint8_t*)i.Data());
    i.RemoveConsumed(4);
    return true;
  }

  bool Write8(uint8_t val) {
    uint8_t* data = (uint8_t*)o.Space(1);
    if (!data) {
      return false;
    }
    *data = val;
    o.Commit(1);
    return true;
  }

  // valid until the ReadXXX call
  template<typename T>
  bool Read(T*& val, int len) {
    if (!i.Read(len)) {
      return false;
    }
    val = reinterpret_cast<T*>(i.Data());
    i.RemoveConsumed(len);
    return true;
  }

  // valid until Drain() or Space() call
  template<typename T>
  bool Write(T*& val, int len) {
    val = reinterpret_cast<T*>(o.Space(len));
    if (!val) {
      return false;
    }
    o.Commit(len);
    return true;
  }

  bool Write(const char* data, int len) {
    return o.WriteExactly(data, len);
  }

  bool Flush() {
    return o.Drain();
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
