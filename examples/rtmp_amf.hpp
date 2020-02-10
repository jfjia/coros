#pragma once

inline uint32_t RB32(const uint8_t* x) {
  return (((uint32_t)x[0] << 24) | (x[1] << 16) | (x[2] <<  8) | x[3]);
}

inline uint32_t RL32(const uint8_t* x) {
  return (((uint32_t)x[3] << 24) | (x[2] << 16) | (x[1] <<  8) | x[0]);
}

inline uint32_t RB24(const uint8_t* x) {
  return ((x[0] << 16) | (x[1] <<  8) | x[2]);
}
