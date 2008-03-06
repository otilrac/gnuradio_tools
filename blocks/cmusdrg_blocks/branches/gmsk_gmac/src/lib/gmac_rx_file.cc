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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <mb_mblock.h>
#include <mb_runtime.h>
#include <mb_protocol_class.h>
#include <mb_exception.h>
#include <mb_msg_queue.h>
#include <mb_message.h>
#include <mb_msg_accepter.h>
#include <mb_class_registry.h>
#include <pmt.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <fstream>

#include <gmsk.h>
#include <gmac.h>
#include <gmac_symbols.h>

static bool verbose = false;

class gmac_rx_file : public mb_mblock
{
  mb_port_sptr 	d_tx;
  mb_port_sptr  d_rx;
  mb_port_sptr 	d_cs;
  pmt_t		d_tx_chan;	// returned tx channel handle

  enum state_t {
    INIT,
    DATA_WAIT,
    SEND_ACK,
  };

  state_t	d_state;
  long		d_nframes_xmitted;
  bool		d_done_sending;

  std::ofstream d_ofile;

  long d_local_addr;
  long d_dst_addr;

 public:
  gmac_rx_file(mb_runtime *runtime, const std::string &instance_name, pmt_t user_arg);
  ~gmac_rx_file();
  void handle_message(mb_message_sptr msg);

 protected:
  void open_usrp();
  void close_usrp();
  void allocate_channel();
  void send_packets();
  void enter_data_wait();
  void build_and_send_ack(long dst);
  void handle_response_rx_pkt(pmt_t data);
  void enter_closing_channel();
};

gmac_rx_file::gmac_rx_file(mb_runtime *runtime, const std::string &instance_name, pmt_t user_arg)
  : mb_mblock(runtime, instance_name, user_arg),
    d_state(INIT), 
    d_nframes_xmitted(0),
    d_done_sending(false)
{ 

  pmt_t file = pmt_nth(0, user_arg);
  d_local_addr = pmt_to_long(pmt_nth(1, user_arg));
  
  // Open a stream to the input file and ensure it's open
  d_ofile.open(pmt_symbol_to_string(file).c_str(), std::ios::binary|std::ios::out);

  if(!d_ofile.is_open()) {
    std::cout << "Error opening input file\n";
    shutdown_all(PMT_F);
    return;
  }
  
  define_component("GMAC", "gmac", pmt_nth(0,user_arg)); // FIXME: RFX2400 Hack
  d_tx = define_port("tx0", "gmac-tx", false, mb_port::INTERNAL);
  d_rx = define_port("rx0", "gmac-rx", false, mb_port::INTERNAL);
  d_cs = define_port("cs", "gmac-cs", false, mb_port::INTERNAL);

  connect("self", "tx0", "GMAC", "tx0");
  connect("self", "rx0", "GMAC", "rx0");
  connect("self", "cs", "GMAC", "cs");

  std::cout << "[GMAC_RX_FILE] Waiting...\n";
}

gmac_rx_file::~gmac_rx_file()
{
  d_ofile.close();
}

void
gmac_rx_file::handle_message(mb_message_sptr msg)
{
  pmt_t event = msg->signal();
  pmt_t data = msg->data();
  pmt_t port_id = msg->port_id();

  pmt_t handle = PMT_F;
  pmt_t status = PMT_F;
  pmt_t dict = PMT_NIL;
  std::string error_msg;
  
  // Dispatch based on state
  switch(d_state) {

    //------------------------------ INIT ---------------------------------//
    // When GMAC is done initializing, it will send a response
    case INIT:
      
      if(pmt_eq(event, s_response_gmac_initialized)) {
        handle = pmt_nth(0, data);
        status = pmt_nth(1, data);

        if(pmt_eq(status, PMT_T)) {
          enter_data_wait();
          return;
        }
        else {
          error_msg = "error initializing gmac:";
          goto bail;
        }
      }
      goto unhandled;

    //-------------------------- DATA_WAIT ----------------------------//
    // In the transmit state we count the number of underruns received and
    // ballpark the number with an expected count (something >1 for starters)
    case DATA_WAIT:
      
      // Check that the transmits are OK
      if (pmt_eq(event, s_response_rx_pkt)){
        handle_response_rx_pkt(data);
        return;
      }
      goto unhandled;

    //----------------------- ACK WAIT ----------------------------------//
    // In the state of waiting for an ACK, to re-enter the transmit state
    case SEND_ACK:

      // Check that the transmits are OK
      if (pmt_eq(event, s_response_tx_pkt)){
        handle = pmt_nth(0, data);
        status = pmt_nth(1, data);

        // If the ACK send was successful, lets go back to waiting for data
        if (pmt_eq(status, PMT_T)){
          d_cs->send(s_cmd_rx_enable, pmt_list1(PMT_NIL));
          enter_data_wait();
          return;
        }
        else {
          error_msg = "bad response-tx-pkt:";
          goto bail;
        }
      }

    goto unhandled;

  }

 // An error occured, print it, and shutdown all m-blocks
 bail:
  std::cerr << error_msg << data
      	    << "status = " << status << std::endl;
  shutdown_all(PMT_F);
  return;

 // Received an unhandled message for a specific state
 unhandled:
  if(verbose && 0 && !pmt_eq(event, pmt_intern("%shutdown")))
    std::cout << "[GMAC_RX_FILE] unhandled msg: " << msg
              << "in state "<< d_state << std::endl;
}

// Wait until we get incoming data...
void
gmac_rx_file::enter_data_wait()
{
  d_state = DATA_WAIT;
}

void
gmac_rx_file::build_and_send_ack(long dst)
{
  size_t ignore;
  long n_bytes;

  d_state = SEND_ACK;

  // Before we send the frame, we stop the RX port since we are not interested
  // in decoding while transmitting, and full processing can go to TX
  d_cs->send(s_cmd_rx_disable, pmt_list1(PMT_NIL));

  // Let's read in as much as possible to fit in a frame
  char data[1];
  n_bytes=1;

  // Make the PMT data, get a writable pointer to it, then copy our data in
  pmt_t uvec = pmt_make_u8vector(n_bytes, 0);
  char *vdata = (char *) pmt_u8vector_writeable_elements(uvec, ignore);
  memcpy(vdata, data, n_bytes);

  // Per packet properties
  pmt_t tx_properties = pmt_make_dict();

  pmt_dict_set(tx_properties, pmt_intern("ack"), PMT_T);

  pmt_t timestamp = pmt_from_long(0xffffffff);	// NOW
  d_tx->send(s_cmd_tx_pkt,
	     pmt_list5(PMT_NIL,   // invocation-handle
           pmt_from_long(d_local_addr),// src
           pmt_from_long(dst),// destination
		       uvec,				    // the samples
           tx_properties)); // per pkt properties

  d_nframes_xmitted++;

  if(verbose)
    std::cout << "[GMAC_RX_FILE] Transmitted ACK from " << d_local_addr
              << " to " << dst
              << std::endl;
}


void
gmac_rx_file::handle_response_rx_pkt(pmt_t data)
{
  pmt_t invocation_handle = pmt_nth(0, data);
  pmt_t payload = pmt_nth(1, data);
  pmt_t pkt_properties = pmt_nth(2, data);

  long lsrc=0, ldst=0;
  bool bcrc=false;

  if(pmt_is_dict(pkt_properties)) {

    if(pmt_t src = pmt_dict_ref(pkt_properties,
                                pmt_intern("src"),
                                PMT_NIL)) {
      if(!pmt_eqv(src, PMT_NIL))
        lsrc = pmt_to_long(src);
    }
    
    if(pmt_t dst = pmt_dict_ref(pkt_properties,
                                pmt_intern("dst"),
                                PMT_NIL)) {
      if(!pmt_eqv(dst, PMT_NIL))
        ldst = pmt_to_long(dst);
    }
    if(pmt_t crc = pmt_dict_ref(pkt_properties,
                                pmt_intern("crc"),
                                PMT_NIL)) {
      if(pmt_eqv(crc, PMT_T))
        bcrc = true;
      else
        bcrc = false;
    }
  }

  // Ensure frame is destined to us, if so we will ACK
  if(ldst != d_local_addr) {
    if(verbose)
      std::cout << "[GMAC_RX_FILE] Received frame not destined to us\n";
    return;
  }
  
  if(verbose)
    std::cout << "[GMAC_RX_FILE] Received frame destined for us!\n";

  // Data was destined for us, let's dump it to the output file
  size_t nbytes;
  char *payload_data = (char *)pmt_u8vector_writeable_elements(payload, nbytes);
  d_ofile.write((const char *)payload_data, nbytes);
  d_ofile.flush();

  // Now that we've determined it's for us, we ACK the source
  if(bcrc)
    build_and_send_ack(lsrc);

  std::cout << ".";
  fflush(stdout);

}

REGISTER_MBLOCK_CLASS(gmac_rx_file);
