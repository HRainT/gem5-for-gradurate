#ifndef __MEM_RXUICACHE_HH__
#define __MEM_RXUICACHE_HH__
#include "mem/cache/cache.hh"
#include "params/RxuICache.hh"

namespace gem5{
class RxuICache:public Cache
{
  public:
    void recvTimingReq(PacketPtr pkt) override;
    void handleTimingReqMiss(PacketPtr pkt, CacheBlk *blk,
                             Tick forward_time,
                             Tick request_time) override;
    RxuICache(const RxuICacheParams &p):Cache(p){}
};

}
#endif