#include <sstream>
#include <iterator>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <fstream>
#include <list>

const int TSAMPLES=23872;
const int COMP_WINDOW=30;
const int NSKIP=5000;
const int POWER_THRESH=1000;

std::list<long> power_history;

enum state_t {
  IDLE,
  MONITORING,
  SKIPPING
};
state_t curr_state;

long tx_average;
long curr_average;
long samples_left;
long ntransmissions;

void check_power(long mf_flag, long power)
{
  long error;

  switch(curr_state) {

    case IDLE:
      if(mf_flag==1) {
        samples_left=TSAMPLES;
        tx_average=curr_average;
        std::cout << ntransmissions++;
        curr_state=MONITORING;
      }
    break;

    case MONITORING:
      error = std::abs(curr_average-tx_average);
      if(error>POWER_THRESH) {
        std::cout << " fail\n";
        samples_left+=NSKIP;
        curr_state=SKIPPING;
      }
      samples_left--;
      if(samples_left==0) {
        std::cout << " success\n";
        curr_state=SKIPPING;
        samples_left=NSKIP;
      }
    break;

    case SKIPPING:
      if(samples_left>0)
        samples_left--;
      else 
        curr_state=IDLE;
    break;

    default:
    break;
  }
}

void compute_average(long power)
{  
  power_history.push_back(power);

  if(power_history.size()<COMP_WINDOW) 
    return;

  power_history.pop_front();

  long long sum;
  std::list<long>::iterator power_it;
  for(power_it = power_history.begin(); power_it != power_history.end(); power_it++)
    sum += *power_it;

  curr_average = sum/COMP_WINDOW;
}

int main(int argc, char **argv) {

  std::string curr_line;

  ntransmissions=0;
  curr_state=IDLE;

  // Input format:
  //   src_ip dst_ip src_port dst_port src_pkts dst_pkts
  while(std::cin) {

    getline(std::cin, curr_line);     // read line from input
    std::istringstream in(curr_line); // put curr line to split

    if(curr_line=="") break;

    std::vector<std::string> tokens;
    for(std::string each; std::getline(in,each,'\t'); tokens.push_back(each));

    // Convert the ports to integers
    long mf_flag, power;
    std::istringstream mf(tokens[0]), pow(tokens[1]);
    mf >> mf_flag;
    pow >> power;

    compute_average(power);
    check_power(mf_flag, power);
  }

  return 1;
}
