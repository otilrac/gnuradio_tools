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
#include <cmac.h>
#include <cmac_symbols.h>
#include <mac_symbols.h>

static bool verbose = false;

class cmac_rx_file : public mb_mblock
{
  mb_port_sptr 	d_tx;
  mb_port_sptr  d_rx;
  mb_port_sptr 	d_cs;
  pmt_t		d_tx_chan;	// returned tx channel handle

  enum state_t {
    INIT,
    DATA_WAIT,
  };

  state_t	d_state;
  long		d_nframes_xmitted;
  bool		d_done_sending;

  std::ofstream d_ofile;

  long d_local_addr;

 public:
  cmac_rx_file(mb_runtime *runtime, const std::string &instance_name, pmt_t user_arg);
  ~cmac_rx_file();
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

cmac_rx_file::cmac_rx_file(mb_runtime *runtime, const std::string &instance_name, pmt_t user_arg)
  : mb_mblock(runtime, instance_name, user_arg),
    d_state(INIT), 
    d_nframes_xmitted(0),
    d_done_sending(false)
{ 
  // Extract USRP information from python and the arguments to the python script
  std::string file = pmt_symbol_to_string(pmt_nth(0, user_arg));
  d_local_addr = pmt_to_long(pmt_nth(1, user_arg));

  // Open a stream to the input file and ensure it's open
  d_ofile.open(file.c_str(), std::ios::binary|std::ios::out);

  if(!d_ofile.is_open()) {
    std::cout << "Error opening input file\n";
    shutdown_all(PMT_F);
    return;
  }

  pmt_t cmac_data = pmt_list1(pmt_from_long(d_local_addr));
  
  define_component("CMAC", "cmac", cmac_data); 
  d_tx = define_port("tx0", "cmac-tx", false, mb_port::INTERNAL);
  d_rx = define_port("rx0", "cmac-rx", false, mb_port::INTERNAL);
  d_cs = define_port("cs", "cmac-cs", false, mb_port::INTERNAL);

  connect("self", "tx0", "CMAC", "tx0");
  connect("self", "rx0", "CMAC", "rx0");
  connect("self", "cs", "CMAC", "cs");

  std::cout << "[CMAC_RX_FILE] Initialized ..."
            << "\n    Filename: " << file
            << "\n    Address: " << d_local_addr
            << "\n";

}

cmac_rx_file::~cmac_rx_file()
{
  d_ofile.close();
}

void
cmac_rx_file::handle_message(mb_message_sptr msg)
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
    // When CMAC is done initializing, it will send a response
    case INIT:
      
      if(pmt_eq(event, s_response_mac_initialized)) {
        handle = pmt_nth(0, data);
        status = pmt_nth(1, data);

        if(pmt_eq(status, PMT_T)) {
          enter_data_wait();
          return;
        }
        else {
          error_msg = "error initializing cmac:";
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
    std::cout << "[CMAC_RX_FILE] unhandled msg: " << msg
              << "in state "<< d_state << std::endl;
}

// Wait until we get incoming data...
void
cmac_rx_file::enter_data_wait()
{
  d_state = DATA_WAIT;
  d_cs->send(s_cmd_rx_enable, pmt_list1(PMT_NIL));
}

void
cmac_rx_file::handle_response_rx_pkt(pmt_t data)
{
  pmt_t invocation_handle = pmt_nth(0, data);
  pmt_t payload = pmt_nth(1, data);
  pmt_t pkt_properties = pmt_nth(2, data);

  // Data was destined for us, let's dump it to the output file
  size_t nbytes;
  char *payload_data = (char *)pmt_u8vector_writeable_elements(payload, nbytes);
  d_ofile.write((const char *)payload_data, nbytes);
  d_ofile.flush();

  std::cout << ".";
  fflush(stdout);

}

int
main (int argc, char **argv)
{
  mb_runtime_sptr rt = mb_make_runtime();
  pmt_t result = PMT_NIL;

  if(argc!=3) {
    std::cout << "usage: ./cmac_rx_file output_file local_addr\n";
    return -1;
  }

  pmt_t args = pmt_list2(pmt_intern(argv[1]), pmt_from_long(strtol(argv[2],NULL,10)));

  rt->run("top", "cmac_rx_file", args, &result);
}

REGISTER_MBLOCK_CLASS(cmac_rx_file);
