/* Copyright 2013 Kjetil S. Matheussen

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. */


#ifndef AUDIO_TYPEPUNNING_H
#define AUDIO_TYPEPUNNING_H

#include <stdint.h>

static inline int pun_float_to_int(float x){
  union { float f; uint32_t i; } vx = { x };
  return vx.i;
}

static inline float pun_int_to_float(int x){
  union { uint32_t i; float f; } mx = { .i=(uint32_t)x };
  return mx.f;
}

#endif
