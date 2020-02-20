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
    std::vector<uint8_t> body;
    for (;;) {
      Header header;
      if (!ReadHeader(io, header)) {
        break;
      }
      if (!ReadBody(io, header, body)) {
        break;
      }
      if (body.size() == header.len) {
        body.clear();
        MALOG_INFO("got packet: type=" << int(header.msg_type) << ",chan=" << header.channel);
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
  if (!io.Read8(c0)) {
    return false;
  }
  MALOG_INFO("handshake, c0=" << std::to_string(c0));
  if (!io.Write8(c0)) {
    MALOG_ERROR("fail to send");
    return false;
  }

  // C1
  Challenge* c1;
  if (!io.Read<Challenge>(c1, RTMP_SIG_SIZE)) {
    return false;
  }
  MALOG_INFO("handshake, c1 time: " << c1->time <<
             ", version: " << static_cast<uint32_t>(c1->version[0]) << "." <<
             static_cast<uint32_t>(c1->version[1]) << "." <<
             static_cast<uint32_t>(c1->version[2]) << "." <<
             static_cast<uint32_t>(c1->version[3]));
  Challenge* s1;
  if (!io.Write<Challenge>(s1, sizeof(Challenge))) {
    MALOG_ERROR("send failed");
    return false;
  }
  s1->time = 0;
  s1->version[0] = 2;
  s1->version[1] = 0;
  s1->version[2] = 0;
  s1->version[3] = 0;

  if (!io.Flush()) {
    MALOG_ERROR("send failed");
    return false;
  }
  if (!io.Write((const char*)c1, RTMP_SIG_SIZE)) {
    return false;
  }

  // C2
  Challenge* c2;
  if (!io.Read<Challenge>(c2, RTMP_SIG_SIZE)) {
    return false;
  }
  MALOG_INFO("handshake, c2 time: " << c2->time <<
             ", version: " << static_cast<uint32_t>(c2->version[0]) << "." <<
             static_cast<uint32_t>(c2->version[1]) << "." <<
             static_cast<uint32_t>(c2->version[2]) << "." <<
             static_cast<uint32_t>(c2->version[3]));
  return true;
}

bool Conn::ReadHeader(IoBuf& io, Header& header) {
  uint8_t flags;
  if (!io.Read8(flags)) {
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
    if (!io.ReadBE24(header.ts)) {
      return false;
    }
    if (header.type != HEADER_4_BYTE) {
      if (!io.ReadBE24(header.len)) {
        return false;
      }
      if (!io.Read8(header.msg_type)) {
        return false;
      }
      if (header.type != HEADER_8_BYTE) {
        if (!io.ReadLE32(header.stream_id)) {
          return false;
        }
      }
    }
  }
  if (header.ts == 0xffffff) {
    uint32_t ts;
    if (!io.ReadBE32(ts)) {
      return false;
    }
    header.final_ts = static_cast<uint64_t>(ts);
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

bool Conn::ReadBody(IoBuf& io, Header& header, std::vector<uint8_t>& body) {
  uint32_t chunk_size = std::min(uint32_t(header.len - body.size()), in_chunk_size_);
  MALOG_INFO("body size=" << header.len << ",already read=" << body.size() << ",try to read=" << chunk_size);
  uint8_t* data;
  if (!io.Read<uint8_t>(data, chunk_size)) {
    return false;
  }
  body.insert(body.end(), data, data + chunk_size);
  return true;
}
