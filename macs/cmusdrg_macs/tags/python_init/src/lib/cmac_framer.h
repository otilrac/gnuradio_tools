/* -*- c++ -*- */
/*
 * Copyright 2007 Free Software Foundation, Inc.
 * 
 * This file is part of GNU Radio
 * 
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef INCLUDED_CMAC_FRAMER_H
#define INCLUDED_CMAC_FRAMER_H

#include <string>
#include <boost/crc.hpp>
#include <boost/cstdint.hpp>
#include <gmsk.h>

// Lengths in bits of our fields used for timestamp calculation
static const long PREAMBLE_LEN=16;
static const long FRAMING_BITS_LEN=64;
static const long POSTAMBLE_LEN=16;

static const int MAX_FRAME_SIZE = 1500;

typedef struct d_frame_hdr_t {
  long src_addr;
  long dst_addr;
  long payload_len;
  bool ack;
  long crc;
} __attribute__((__packed__));

#endif
