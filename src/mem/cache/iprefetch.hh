#ifndef __MEM_IPREFETCH_HH__
#define __MEM_IPREFETCH_HH__
#include "mem/cache/cache.hh"
#include "params/IPrefetch.hh"

namespace gem5{
class IPrefetch:public Cache
{
  public:
    class IcacheReqPort : public RequestPort // link to cpu
    {
      protected:
        /** Pointer to fetch. */
        IPrefetch *_iprefetch;

      public:
        /** Default constructor. */
        IcacheReqPort(IPrefetch *iprefetch):RequestPort("iprefetch.icache_reqport"),_iprefetch(iprefetch)
        {};

      protected:

        /** Timing version of receive.  Handles setting fetch to the
         * proper status to start fetching. */
        virtual bool recvTimingResp(PacketPtr pkt);
        
        /** Handles doing a retry of a failed fetch. */
        virtual void recvReqRetry();
    };
    
    class IcacheRespPort : public ResponsePort // link to cpu
    {
      protected:
        /** Pointer to fetch. */
        IPrefetch *_iprefetch;

      public:
        /** Default constructor. */
        IcacheRespPort(IPrefetch *iprefetch):ResponsePort("iprefetch.icache_respport"),_iprefetch(iprefetch)
        {};

      protected:
        virtual bool recvTimingSnoopResp(PacketPtr pkt) override;

        virtual bool tryTiming(PacketPtr pkt) override;

        virtual bool recvTimingReq(PacketPtr pkt) override;

        virtual Tick recvAtomic(PacketPtr pkt) override;

        virtual void recvFunctional(PacketPtr pkt) override;

        virtual AddrRangeList getAddrRanges() const override;

        virtual void recvRespRetry();
    };


    IcacheReqPort  icachereqport;
    IcacheRespPort icacherespport;
    
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
    
    void recvTimingResp(PacketPtr pkt)override;
    void handleTimingReqMiss(PacketPtr pkt, CacheBlk *blk,
                             Tick forward_time,
                             Tick request_time) override;
    void handleTimingReqHit(PacketPtr pkt, CacheBlk *blk,
                             Tick request_time, bool first_acc_after_pf) override;
    IPrefetch(const IPrefetchParams &p):Cache(p),icachereqport(this),icacherespport(this){};
    void tick();
    
    void recvTimingReq(PacketPtr pkt)override;

    void recvTimingSnoopReq(PacketPtr pkt) override;
    class FtqEntry
    {
      public:
        static const uint64_t tag_offset = 9;  
        bool vld = false;
        Addr tag_va;
        Addr tag_pa;
        uint64_t pending_bits[8];
        bool fetch_bits[8];
        bool incache_bits[8];
        uint64_t bb_offset;
        uint64_t bb_size;
        Addr next_bb_pc;
        Addr next_bb_index;
      public:
        bool inpending(){
          for(int i=0;i<8;i++)
            if(pending_bits[i] == 1 || pending_bits[i] == 2)return true;
          return false;          
        }
        void init(Addr ppc){
            vld = true;
            tag_pa= ppc >> tag_offset;
            for(int i=0;i<8;i++){
              pending_bits[i]=0;
              incache_bits[i]=fetch_bits[i]=false;
            }
            bb_offset= (ppc >> 6) % 8 ;
            fetch_bits[bb_offset]=true;
            pending_bits[bb_offset]=1;
            next_bb_pc = -1;
            next_bb_index = -1;
            bb_size=8;
        }
    };
    
    class FtqPrefetch{
        public:
        static const uint64_t tag_offset = 9;  
        static const uint64_t ftqentrynums = 16;  
        static const uint64_t ftqentry_size = 8;
        
        uint64_t count=7;
        FtqEntry ftqentry[ftqentrynums];
        // to find start pc
        Addr prefrethpc=0;
        // entry to send prefetch
        int prefetch_entry=0;
        // for ftq end entry
        int active_entry=-1;
        
        // clean of not clean
        bool empty(){
            for(int i=0;i<ftqentrynums;i++){
              if(ftqentry[i].vld)return false;
            }
            return true;
        }
        int findevict(){
            for(int i=0;i<ftqentrynums;i++){
              if(!ftqentry[i].vld)return i;
            }
            for(int i=0;i<ftqentrynums;i++){
              if(ftqentry[i].inpending())continue;
              return i;
            }
            return -1;
        }
        // new fetch in ftq
        void ftqinitfetch(Addr ppc);
        void fetchresp(Addr ppc);
        Addr genprefetch();
    };
    FtqPrefetch ftqprefetch;
};

}
#endif