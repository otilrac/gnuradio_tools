#include <iostream>
#include <fstream>
#include <complex>

typedef std::complex<float> data_t;

int main(int argc, char *argv[]) {

  if(argc != 2) {
    std::cout << "Usage: ./read_complex <data_file>\n";
    return -1;
  }

  std::ifstream infile;

  infile.open(argv[1], std::ios::binary|std::ios::in);
  if(!infile.is_open())
    return -1;
    
  data_t curr;

  infile.read((char *)&curr, sizeof(data_t));

  while(!infile.eof()) {
    printf("%.10f %.10f\n", curr.real(), curr.imag());
    infile.read((char *)&curr, sizeof(data_t));
  }

  infile.close();
}
