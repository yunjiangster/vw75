/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD
license as described in the file LICENSE.
 */
#ifndef BS_H
#define BS_H

#define BS_TYPE_MEAN 0
#define BS_TYPE_VOTE 1

#include "global_data.h"

namespace BS
{
  LEARNER::learner* setup(vw& all, std::vector<std::string>&, po::variables_map& vm, po::variables_map& vm_file);
  void print_result(int f, float res, float weight, v_array<char> tag, float lb, float ub);
  
  void output_example(vw& all, example* ec, float lb, float ub);

  inline uint32_t weight_gen()//sampling from Poisson with rate 1
  { 
    float temp = frand48();
    if(temp<=0.3678794411714423215955) return 0;
    if(temp<=0.735758882342884643191)  return 1;
    if(temp<=0.919698602928605803989)  return 2;
    if(temp<=0.9810118431238461909214) return 3;
    if(temp<=0.9963401531726562876545) return 4;
    if(temp<=0.9994058151824183070012) return 5;
    if(temp<=0.9999167588507119768923) return 6;
    if(temp<=0.9999897508033253583053) return 7;
    if(temp<=0.9999988747974020309819) return 8;
    if(temp<=0.9999998885745216612793) return 9;
    if(temp<=0.9999999899522336243091) return 10;
    if(temp<=0.9999999991683892573118) return 11;
    if(temp<=0.9999999999364022267287) return 12;
    if(temp<=0.999999999995480147453) return 13;
    if(temp<=0.9999999999996999989333) return 14;
    if(temp<=0.9999999999999813223654) return 15;
    if(temp<=0.9999999999999989050799) return 16;
    if(temp<=0.9999999999999999393572) return 17;
    if(temp<=0.999999999999999996817)  return 18;
    if(temp<=0.9999999999999999998412) return 19;
    return 20;
  }
}

#endif
