#include "malog.h"
#include "rtmp_conn.hpp"
#include "rtmp_amf.hpp"

bool Conn::Start(uv_os_sock_t fd) {
  if (!coro_.Create(coros::Scheduler::Get(),
                    std::bind(&Conn::Fn, this, fd),
                    std::bind(&Conn::ExitFn, this))) {
    return false;
  }
  return true;
}

void Conn::Fn(uv_os_sock_t fd) {
  coros::Socket s(fd);
  IoBuf io(s);
  s.SetDeadline(30);

  if (Handshake(io)) {
    MALOG_INFO("handshake done");
    for (;;) {
      Header header;
      if (!ReadHeader(io, header)) {
        break;
      }
    }
  }

  s.Close();
}

void Conn::ExitFn() {
  MALOG_INFO("coro[" << coro_.GetId() << "] exit");
  delete this;
}

bool Conn::Handshake(IoBuf& io) {
  // C0
  uint8_t c0;
  if (!io.i.Read8(c0)) {
    return false;
  }
  MALOG_INFO("handshake, c0=" << std::to_string(c0));
  if (!io.o.Write8(c0)) {
    MALOG_ERROR("fail to send");
    return false;
  }

  // C1
  if (!io.i.Read(RTMP_SIG_SIZE)) {
    return false;
  }
  Challenge* c1 = (Challenge*)io.i.Data();
  MALOG_INFO("handshake, c1 time: " << c1->time <<
             ", version: " << static_cast<uint32_t>(c1->version[0]) << "." <<
             static_cast<uint32_t>(c1->version[1]) << "." <<
             static_cast<uint32_t>(c1->version[2]) << "." <<
             static_cast<uint32_t>(c1->version[3]));
  Challenge* s1 = (Challenge*)io.o.Space(sizeof(Challenge));
  if (!s1) {
    MALOG_ERROR("send failed");
    return false;
  }
  s1->time = 0;
  s1->version[0] = 2;
  s1->version[1] = 0;
  s1->version[2] = 0;
  s1->version[3] = 0;
  io.o.Commit(sizeof(Challenge));
  if (!io.o.Drain()) {
    MALOG_ERROR("send failed");
    return false;
  }
  if (!io.o.WriteExactly((const char*)c1, RTMP_SIG_SIZE)) {
    return false;
  }
  io.i.RemoveConsumed(RTMP_SIG_SIZE);

  // C2
  if (!io.i.Read(RTMP_SIG_SIZE)) {
    return false;
  }
  Challenge* c2 = (Challenge*)io.i.Data();
  MALOG_INFO("handshake, c2 time: " << c2->time <<
             ", version: " << static_cast<uint32_t>(c2->version[0]) << "." <<
             static_cast<uint32_t>(c2->version[1]) << "." <<
             static_cast<uint32_t>(c2->version[2]) << "." <<
             static_cast<uint32_t>(c2->version[3]));
  io.i.RemoveConsumed(RTMP_SIG_SIZE);
  return true;
}

bool Conn::ReadHeader(IoBuf& io, Header& header) {
  uint8_t flags;
  if (!io.i.Read8(flags)) {
    return false;
  }

  header.channel = static_cast<uint32_t>(flags & 0x3F);
  header.type = static_cast<uint8_t>(flags >> 6);
  if (header.channel < 2) {
    MALOG_ERROR("channel=" << header.channel << ", > 64 channels not support");
    return false;
  }
  MALOG_INFO("header.channel=" << int(header.channel) << ", type=" << int(header.type));
  header.len  = headers_[header.channel].len;
  header.msg_type  = headers_[header.channel].msg_type;
  header.stream_id = headers_[header.channel].stream_id;
  header.ts = headers_[header.channel].ts;
  if (header.type != HEADER_1_BYTE) {
    if (!io.i.Read(3)) {
      return false;
    }
    header.ts = RB24((const uint8_t*)io.i.Data());
    io.i.RemoveConsumed(3);
    if (header.type != HEADER_4_BYTE) {
      if (!io.i.Read(4)) {
        return false;
      }
      header.len = RB24((const uint8_t*)io.i.Data());
      io.i.RemoveConsumed(3);
      header.msg_type = *((uint8_t*)io.i.Data());
      io.i.RemoveConsumed(1);
      if (header.type != HEADER_8_BYTE) {
        if (!io.i.Read(4)) {
          return false;
        }
        header.stream_id = RL32((const uint8_t*)io.i.Data());
        io.i.RemoveConsumed(4);
      }
    }
  }
  if (header.ts == 0xffffff) {
    if (!io.i.Read(4)) {
      return false;
    }
    header.final_ts = RB32((const uint8_t*)io.i.Data());
    io.i.RemoveConsumed(4);
  } else {
    header.final_ts = header.ts;
  }
  if (header.type != HEADER_12_BYTE) {
    header.final_ts += headers_[header.channel].final_ts;
  }
  MALOG_INFO("msg_type=" << int(header.msg_type) << ",len=" << int(header.len));
  if (header.type != HEADER_1_BYTE) {
    headers_[header.channel] = header;
  }
  return true;
}
