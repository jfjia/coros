#include "rtmpd.hpp"

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
  s.SetDeadline(30);
  do {
    if (!HandshakeC0(s)) {
      break;
    }
    if (!HandshakeC1(s)) {
      break;
    }
    if (!HandshakeC2(s)) {
      break;
    }
    MALOG_INFO("handshake done");
    HandlePacket(s);
  } while (0);

  s.Close();
}

void Conn::ExitFn() {
  MALOG_INFO("coro[" << coro_.GetId() << "] exit");
  delete this;
}

bool Conn::HandshakeC0(coros::Socket& s) {
  uint8_t c0;
  if (rb_.Read(s, 1) < 1) {
    return false;
  }
  c0 = *((uint8_t*)rb_.Data());
  rb_.RemoveConsumed(1);
  MALOG_INFO("handshake, c0=" << std::to_string(c0));
  uint8_t* o = (uint8_t*)wb_.Space(s, 1);
  if (!o) {
    MALOG_ERROR("fail to send");
    return false;
  }
  *o = c0;
  wb_.Commit(1);
  return true;
}

bool Conn::HandshakeC1(coros::Socket& s) {
  if (rb_.Read(s, RTMP_SIG_SIZE) < RTMP_SIG_SIZE) {
    return false;
  }
  Challenge* c1 = (Challenge*)rb_.Data();
  MALOG_INFO("handshake, c1 time: " << c1->time <<
             ", version: " << static_cast<uint32_t>(c1->version[0]) << "." <<
             static_cast<uint32_t>(c1->version[1]) << "." <<
             static_cast<uint32_t>(c1->version[2]) << "." <<
             static_cast<uint32_t>(c1->version[3]));
  Challenge* s1 = (Challenge*)wb_.Space(s, sizeof(Challenge));
  if (!s1) {
    MALOG_ERROR("send failed");
    return false;
  }
  s1->time = 0;
  s1->version[0] = 2;
  s1->version[1] = 0;
  s1->version[2] = 0;
  s1->version[3] = 0;
  wb_.Commit(sizeof(Challenge));
  if (wb_.Drain(s) <= 0) {
    MALOG_ERROR("send failed");
    return false;
  }
  if (s.WriteExactly((const char*)c1, RTMP_SIG_SIZE) != RTMP_SIG_SIZE) {
    return false;
  }
  rb_.RemoveConsumed(RTMP_SIG_SIZE);
  return true;
}

bool Conn::HandshakeC2(coros::Socket& s) {
  if (rb_.Read(s, RTMP_SIG_SIZE) < RTMP_SIG_SIZE) {
    return false;
  }
  Challenge* c2 = (Challenge*)rb_.Data();
  MALOG_INFO("handshake, c2 time: " << c2->time <<
             ", version: " << static_cast<uint32_t>(c2->version[0]) << "." <<
             static_cast<uint32_t>(c2->version[1]) << "." <<
             static_cast<uint32_t>(c2->version[2]) << "." <<
             static_cast<uint32_t>(c2->version[3]));
  rb_.RemoveConsumed(RTMP_SIG_SIZE);
  return true;
}

void Conn::HandlePacket(coros::Socket& s) {
  Header header;
  for (;;) {
    if (!ReadHeader(s, header)) {
      break;
    }
  }
}

bool Conn::ReadHeader(coros::Socket& s, Header& header) {
  if (rb_.Read(s, 1) < 1) {
    return false;
  }
  uint8_t flags = *((uint8_t*)rb_.Data());
  rb_.RemoveConsumed(1);
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
    if (rb_.Read(s, 3) < 3) {
      return false;
    }
    header.ts = RB24((const uint8_t*)rb_.Data());
    rb_.RemoveConsumed(3);
    if (header.type != HEADER_4_BYTE) {
      if (rb_.Read(s, 4) < 4) {
        return false;
      }
      header.len = RB24((const uint8_t*)rb_.Data());
      rb_.RemoveConsumed(3);
      header.msg_type = *((uint8_t*)rb_.Data());
      rb_.RemoveConsumed(1);
      if (header.type != HEADER_8_BYTE) {
        if (rb_.Read(s, 4) < 4) {
          return false;
        }
        header.stream_id = RL32((const uint8_t*)rb_.Data());
        rb_.RemoveConsumed(4);
      }
    }
  }
  if (header.ts == 0xffffff) {
    if (rb_.Read(s, 4) < 4) {
      return false;
    }
    header.final_ts = RB32((const uint8_t*)rb_.Data());
    rb_.RemoveConsumed(4);
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

bool Listener::Start() {
  if (!coro_.Create(coros::Scheduler::Get(),
                    std::bind(&Listener::Fn, this),
                    std::bind(&Listener::ExitFn, this))) {
    return false;
  }
  return true;
}

void Listener::Fn() {
  coros::Socket s;
  s.ListenByIp("0.0.0.0", 9090);
  for (;;) {
    uv_os_sock_t s_new = s.Accept();
    MALOG_INFO("coro[" << coro_.GetId() << ": accept new " << s_new);
    if (s_new == BAD_SOCKET) {
      break;
    }
    Conn* c = new Conn();
    c->Start(s_new);
  }
}

void Listener::ExitFn() {
}

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, true);
  coros::Scheduler sched(true);
  Listener l;
  l.Start();
  sched.Run();
  return 0;
}
