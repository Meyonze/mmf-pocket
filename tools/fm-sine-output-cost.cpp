// SPDX-License-Identifier: Apache-2.0
// Single-thread host bus-only timing; setup/source synthesis/I/O excluded.
#include "ma_sine_output.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <new>
size_t allocations=0;
void* operator new(size_t n){++allocations;void* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
using namespace fxchain::smaf::sine_output;
int main(){bool first=true;double checksum=0;std::cout<<std::setprecision(12)<<"{\"scope\":\"Shared host, bus only, no Android speed or RSS claim; ordinary C++ new counter\",\"frames\":44100,\"results\":[";
 for(int repeat=0;repeat<3;++repeat)for(int factor:{2,4,8,16,32})for(int mode:{0,1,2}) {
  Bus bus(factor);std::array<Stereo,32> x{};size_t before=allocations;auto start=std::chrono::steady_clock::now();
  for(int n=0;n<44100;++n){x.fill({});if(mode==2||(mode==1&&n%1024==0))x[size_t(n%factor)]={.3,-.2};
   auto y=bus.frame(std::span(x.data(),size_t(factor)),{.001,-.002});checksum+=y.left+y.right;}
  auto count=allocations-before;double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  if(!first)std::cout<<',';first=false;std::cout<<"{\"repeat\":"<<repeat<<",\"factor\":"<<factor<<",\"mode\":"<<mode<<",\"ms\":"<<ms<<",\"ordinary_new_calls\":"<<count<<'}';
 }
 std::cout<<"],\"checksum\":"<<checksum<<"}\n";
}
