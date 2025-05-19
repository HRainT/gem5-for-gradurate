#include "mem/cache/rxuicache.hh"
namespace gem5{

void
RxuICache::handleTimingReqMiss(PacketPtr pkt, CacheBlk *blk,
    Tick forward_time,
    Tick request_time){
        return;
}
void
RxuICache::recvTimingReq(PacketPtr pkt){
    if(pkt->cmd == MemCmd::RxuIcacheRefill){
        Cycles tag_latency(0);
        Tick request_time = clockEdge();
        CacheBlk *blk = tags->findBlock(pkt->getAddr(), pkt->isSecure());
        MSHR *mshr = mshrQueue.findMatch(pkt->getAddr(), pkt->isSecure());
        if(!mshr)
            // mshr = allocateMissBuffer(pkt,request_time);
            mshr = mshrQueue.allocate(pkt->getBlockAddr(blkSize), blkSize,
                                pkt, request_time, order++,
                                allocOnFill(pkt->cmd));
        PacketList writebacks;
        const bool allocate = (writeAllocator && mshr->wasWholeLineWrite) ?
        writeAllocator->allocate() : mshr->allocOnFill();
        blk = handleFill(pkt, blk, writebacks, allocate);
        mshr->extractServiceableTargets(pkt);
        mshrQueue.deallocate(mshr);
        delete pkt;
    }else{
        Cache::recvTimingReq(pkt);
    }
}

}