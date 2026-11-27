#pragma once
#include <cstring>
#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(p) (*(p))
#define pgm_read_word(p) (*(p))
#define pgm_read_ptr(p) (*(p))
#define memcpy_P std::memcpy
