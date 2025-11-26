#include "mem/cache/iprefetch.hh"
#include "debug/RxuIPrefetch.hh"
namespace gem5{

bool
IPrefetch::IcacheReqPort::recvTimingResp(PacketPtr pkt)
{
    if(pkt->req){
        //icache hit
        Tick request_time = 500;
        _iprefetch->cpuSidePort.schedTimingResp(pkt, curTick()+request_time);
    }
    return true;
}

void
IPrefetch::IcacheReqPort::recvReqRetry()
{
    return;
}


bool
IPrefetch::IcacheRespPort::recvTimingSnoopResp(PacketPtr pkt)
{
    return true;
}


bool
IPrefetch::IcacheRespPort::tryTiming(PacketPtr pkt)
{
    return true;
}

bool
IPrefetch::IcacheRespPort::recvTimingReq(PacketPtr pkt)
{
    return false;
}

Tick
IPrefetch::IcacheRespPort::recvAtomic(PacketPtr pkt)
{
    return 0;
}

void
IPrefetch::IcacheRespPort::recvFunctional(PacketPtr pkt)
{
    return ;
}

AddrRangeList
IPrefetch::IcacheRespPort::getAddrRanges() const
{
    return _iprefetch->getAddrRanges();
}

void IPrefetch::IcacheRespPort::recvRespRetry(){

}
Port &
IPrefetch::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "icache_reqside") {
        return icachereqport;
    } else if(if_name =="icache_respside"){
        return icacherespport;
    } else if (if_name == "mem_side") {
        return memSidePort;
    } else if (if_name == "cpu_side") {
        return cpuSidePort;
    }  else {
        return ClockedObject::getPort(if_name, idx);
    }
}

std::vector<CacheBlk *> blkptrvec;
void get_vldblk(CacheBlk &blk) {
    // find a valid blk
    if(blk.isValid()){
        blkptrvec.push_back(&blk);
    }
}

void IPrefetch::tick(){
    blkptrvec.clear();
    tags->forEachBlk(get_vldblk);
    if(!blkptrvec.empty()){
        int x = rand()%blkptrvec.size();
        CacheBlk *blkptr = blkptrvec[x];
        Addr addr = blkptr->getTag() << 6;
        RequestPtr mem_req = std::make_shared<Request>(addr, blkSize,
                                    Request::INST_FETCH, 0);
        PacketPtr refillpkt = new Packet(mem_req, MemCmd::RxuIcacheRefill);
        refillpkt->dataDynamic(new uint8_t[blkSize]);
        refillpkt->setData(blkptr->data, 0, 0, blkSize);
        DPRINTF(RxuIPrefetch, "addr %#lx blk refill first\n ", addr);
        icachereqport.sendTimingReq(refillpkt);
    }
    Addr prefecthaddr = ftqprefetch.genprefetch();
    if(prefecthaddr!= -1){
        RequestPtr mem_req = std::make_shared<Request>(prefecthaddr, blkSize,
                                Request::INST_FETCH, 0);
        PacketPtr iprefetchpkt = new Packet(mem_req, MemCmd::RxuIcachePrefetch);
        iprefetchpkt->dataDynamic(new uint8_t[blkSize]);
        DPRINTF(RxuIPrefetch, "addr %#lx ftq send a req ,cout is %d \n", prefecthaddr, ftqprefetch.count);
        recvTimingReq(iprefetchpkt);
    }
}

void IPrefetch::recvTimingResp(PacketPtr pkt){
    ftqprefetch.fetchresp(pkt->getAddr());
    DPRINTF(RxuIPrefetch, "addr %#lx streambuffer get fetch response cout is %d \n", pkt->getAddr(), ftqprefetch.count);
    BaseCache::recvTimingResp(pkt);
}

void
IPrefetch::handleTimingReqMiss(PacketPtr pkt, CacheBlk *blk,
    Tick forward_time,
    Tick request_time){
    icachereqport.sendTimingReq(pkt);

    if( pkt->cmd != MemCmd::ReadResp && pkt->cmd != MemCmd::RxuIcachePrefetchResp){// icache miss
        if(pkt->cmd != MemCmd::RxuIcachePrefetch){//fetch
            ftqprefetch.ftqinitfetch(pkt->getAddr());
        }
        DPRINTF(RxuIPrefetch, "addr %#lx Icache Miss\n", pkt->getAddr());
        Cache::handleTimingReqMiss(pkt,blk,forward_time,forward_time);
    }else{
        if(pkt->cmd == MemCmd::RxuIcachePrefetchResp)
            ftqprefetch.fetchresp(pkt->getAddr());
        DPRINTF(RxuIPrefetch, "addr %#lx Icache Hit\n" , pkt->getAddr());
    }
}
void
IPrefetch::handleTimingReqHit(PacketPtr pkt, CacheBlk *blk,
    Tick request_time, bool first_acc_after_pf){
    if(pkt->cmd == MemCmd::RxuIcachePrefetch){
        ftqprefetch.fetchresp(pkt->getAddr());
        DPRINTF(RxuIPrefetch, "addr %#lx ftq hit a resp ,cout is %d \n", pkt->getAddr(), ftqprefetch.count);
        delete pkt;
        return;
    }
    Cache::handleTimingReqHit(pkt,blk,request_time,first_acc_after_pf);
}
void
IPrefetch::recvTimingReq(PacketPtr pkt){
    Cache::recvTimingReq(pkt);
}

void
IPrefetch::recvTimingSnoopReq(PacketPtr pkt){
    icacherespport.sendTimingSnoopReq(pkt);
    Cache::recvTimingSnoopReq(pkt);
}


// fetch miss occurpy
void
IPrefetch::FtqPrefetch::ftqinitfetch(Addr ppc){
    // update bpuprepc
    Addr tag_pa = ppc >> tag_offset;
    // bb offset
    int bb_offset = (ppc >> 6) & (0b111);
    // ftqentry index
    int index;

    index = -1;
    for(int i=0;i<ftqentrynums;i++){
        if(tag_pa == ftqentry[i].tag_pa && ftqentry[i].vld){
            index = i;
            ftqentry[index].fetch_bits[bb_offset]=true;
            if(ftqentry[index].pending_bits[bb_offset] == 0) {
                ftqentry[index].pending_bits[bb_offset] = 1;
            }
        }
    }
    if(index != -1)return;
    index = findevict();
    if(index == -1)return;
    active_entry=index;
    ftqentry[index].bb_size=bb_offset;
    ftqentry[index].init(ppc);
}

void
IPrefetch::FtqPrefetch::fetchresp(Addr ppc){
    for(int i=0;i<ftqentrynums;i++){
        // update bpuprepc
        Addr tag_pa = ppc >> tag_offset;
        // bb offset
        int bb_offset = (ppc >> 6) & (0b111);
        if(tag_pa == ftqentry[i].tag_pa && ftqentry[i].vld){
            int index = i;
            if(ftqentry[index].pending_bits[bb_offset] == 2){
                count = (count + 1);
            }
            ftqentry[index].pending_bits[bb_offset]=3;
        }
    }
}

Addr
IPrefetch::FtqPrefetch::genprefetch(){
    Addr res = -1;
    if(count==0 || active_entry == -1)return res;
    count -= 1;
    int index=active_entry;
    for(int i=ftqentry[index].bb_offset + 1;i<ftqentry_size;i++){
        if(ftqentry[index].pending_bits[i]==0){
            ftqentry[index].pending_bits[i]=2;
            return (ftqentry[index].tag_pa << tag_offset) + (i<<6);
        }
    }
    count += 1;

    return res;
}

}
