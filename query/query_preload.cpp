// SPDX-License-Identifier: MIT
// Linux dedicated-server companion: loaded explicitly, never installed into libc.
#include "query_packet.h"
#include <dlfcn.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>
#include <cstring>
#include <cerrno>
namespace {
pthread_mutex_t guard = PTHREAD_MUTEX_INITIALIZER;
struct Lock { Lock() { pthread_mutex_lock(&guard); } ~Lock() { pthread_mutex_unlock(&guard); } };
double Now() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
BotQuery::Bot snapshot[32] = {};
size_t size=0;
bool enabled=false;
double updated=0;
}
extern "C" void regamedll_query_snapshot_v1(int active, const BotQuery::Bot *bots, size_t count) {
 Lock lock;
 enabled=active!=0; size=count>32?32:count;
 if(size) std::memcpy(snapshot,bots,size*sizeof(*bots));
 updated=Now();
}
extern "C" ssize_t sendto(int socket,const void *data,size_t length,int flags,const sockaddr *to,socklen_t tolen) {
 using Send=ssize_t(*)(int,const void*,size_t,int,const sockaddr*,socklen_t);
 static Send original=reinterpret_cast<Send>(dlsym(RTLD_NEXT,"sendto"));
 if(!original) { errno=ENOSYS; return -1; }
 unsigned char copy[65536];
 if(length>=6 && length<=sizeof(copy) && !std::memcmp(data,"\xff\xff\xff\xff",4)) {
  bool changed=false;
  {
   Lock lock;
   if(enabled && Now()-updated<2.0) {
    std::memcpy(copy,data,length);
    changed=BotQuery::Rewrite(copy,length,snapshot,size);
   }
  }
  if(changed) return original(socket,copy,length,flags,to,tolen);
 }
 return original(socket,data,length,flags,to,tolen);
}
