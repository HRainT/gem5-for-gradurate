#include "cpu/rxuo3/store_set.hh"

#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/RxuStoreSet.hh"
#include <chrono>

namespace gem5
{

namespace rxuo3
{

StoreSet::StoreSet(uint64_t clear_period, int _SSIT_size, int _LFST_size)
    : clearPeriod(clear_period), SSITSize(_SSIT_size), LFSTSize(_LFST_size)
{
    DPRINTF(RxuStoreSet, "StoreSet: Creating store set object.\n");
    DPRINTF(RxuStoreSet, "StoreSet: SSIT size: %i, LFST size: %i.\n",
            SSITSize, LFSTSize);

    if (!isPowerOf2(SSITSize)) {
        fatal("Invalid SSIT size!\n");
    }

    SSIT.resize(SSITSize);

    validSSIT.resize(SSITSize);

    for (int i = 0; i < SSITSize; ++i)
        validSSIT[i] = false;

    if (!isPowerOf2(LFSTSize)) {
        fatal("Invalid LFST size!\n");
    }

    LFST.resize(LFSTSize);

    validLFST.resize(LFSTSize);

    for (int i = 0; i < LFSTSize; ++i) {
        validLFST.push_back(std::unordered_map<InstSeqNum, bool>());
        LFST.push_back(std::list<InstSeqNum>());
    }

    indexMask = SSITSize - 1;

    offsetBits = 2;

    memOpsPred = 0;
}

StoreSet::~StoreSet()
{
}

void
StoreSet::init(uint64_t clear_period, int _SSIT_size, int _LFST_size)
{
    SSITSize = _SSIT_size;
    LFSTSize = _LFST_size;
    clearPeriod = clear_period;

    DPRINTF(RxuStoreSet, "StoreSet: Creating store set object.\n");
    DPRINTF(RxuStoreSet, "StoreSet: SSIT size: %i, LFST size: %i.\n",
            SSITSize, LFSTSize);

    SSIT.resize(SSITSize);

    validSSIT.resize(SSITSize);

    for (int i = 0; i < SSITSize; ++i)
        validSSIT[i] = false;

    LFST.resize(LFSTSize);

    validLFST.resize(LFSTSize);

    for (int i = 0; i < LFSTSize; ++i) {
        validLFST.push_back(std::unordered_map<InstSeqNum, bool>());
        LFST.push_back(std::list<InstSeqNum>());
    }

    indexMask = SSITSize - 1;

    offsetBits = 2;

    memOpsPred = 0;
}


void
StoreSet::violation(Addr store_PC, Addr load_PC)
{
    int load_index = calcIndex(load_PC);
    int store_index = calcIndex(store_PC);

    assert(load_index < SSITSize && store_index < SSITSize);

    bool valid_load_SSID = validSSIT[load_index];
    bool valid_store_SSID = validSSIT[store_index];

    if (!valid_load_SSID && !valid_store_SSID) {
        // Calculate a new SSID here.
        //SSID new_set = calcSSID(load_PC);
        SSID new_set = calcSSID(load_PC);

        validSSIT[load_index] = true;

        SSIT[load_index] = new_set;

        validSSIT[store_index] = true;

        SSIT[store_index] = new_set;

        assert(new_set < LFSTSize);

        DPRINTF(RxuStoreSet, "StoreSet: Neither load nor store had a valid "
                "storeset, creating a new one: %i for load %#x, store %#x\n",
                new_set, load_PC, store_PC);
    } else if (valid_load_SSID && !valid_store_SSID) {
        SSID load_SSID = SSIT[load_index];

        validSSIT[store_index] = true;

        SSIT[store_index] = load_SSID;

        assert(load_SSID < LFSTSize);

        DPRINTF(RxuStoreSet, "StoreSet: Load had a valid store set.  Adding "
                "store to that set: %i for load %#x, store %#x\n",
                load_SSID, load_PC, store_PC);
    } else if (!valid_load_SSID && valid_store_SSID) {
        SSID store_SSID = SSIT[store_index];

        validSSIT[load_index] = true;

        SSIT[load_index] = store_SSID;

        DPRINTF(RxuStoreSet, "StoreSet: Store had a valid store set: %i for "
                "load %#x, store %#x\n",
                store_SSID, load_PC, store_PC);
    } else {
        SSID load_SSID = SSIT[load_index];
        SSID store_SSID = SSIT[store_index];

        assert(load_SSID < LFSTSize && store_SSID < LFSTSize);

        // The store set with the lower number wins
        if (store_SSID > load_SSID) {
            SSIT[store_index] = load_SSID;

            DPRINTF(RxuStoreSet, "StoreSet: Load had smaller store set: %i; "
                    "for load %#x, store %#x\n",
                    load_SSID, load_PC, store_PC);
        } else {
            SSIT[load_index] = store_SSID;

            DPRINTF(RxuStoreSet, "StoreSet: Store had smaller store set: %i; "
                    "for load %#x, store %#x\n",
                    store_SSID, load_PC, store_PC);
        }
    }
}

void
StoreSet::checkClear()
{
    memOpsPred++;
    if (memOpsPred > clearPeriod) {
        DPRINTF(RxuStoreSet, "Wiping predictor state beacuse %d ld/st executed\n",
                clearPeriod);
        memOpsPred = 0;
        clear();
    }
}

void
StoreSet::insertLoad(Addr load_PC, InstSeqNum load_seq_num)
{
    checkClear();
    // Does nothing.
    return;
}

void
StoreSet::insertStore(Addr store_PC, InstSeqNum store_seq_num, ThreadID tid)
{
     int index = calcIndex(store_PC);

    int store_SSID;

    checkClear();
    assert(index < SSITSize);

    if (!validSSIT[index]) {
        // Do nothing if there's no valid entry.
        return;
    } else {
        store_SSID = SSIT[index];

        assert(store_SSID < LFSTSize);

        // Update the last store that was fetched with the current one.
        LFST[store_SSID].push_back(store_seq_num);

        validLFST[store_SSID].emplace(store_seq_num,1);

        storeList[store_seq_num] = store_SSID;

        DPRINTF(RxuStoreSet, "Store %#x updated the LFST, SSID: %i\n",
                store_PC, store_SSID);
    }
}

InstSeqNum
StoreSet::checkInst(Addr PC)
{
    int index = calcIndex(PC);

    int inst_SSID;

    assert(index < SSITSize);

    if (!validSSIT[index]) {
        DPRINTF(RxuStoreSet, "Inst %#x with index %i had no SSID\n",
                PC, index);

        // Return 0 if there's no valid entry.
        return 0;
    } else {
        inst_SSID = SSIT[index];

        assert(inst_SSID < LFSTSize);

        bool has_valid_lfst = false;

        for (auto itvalid = validLFST[inst_SSID].begin();itvalid != validLFST[inst_SSID].end(); ++itvalid){
            if((*itvalid).second == true){
                has_valid_lfst = true;
            }
        }

        if (!has_valid_lfst) {

            DPRINTF(RxuStoreSet, "Inst %#x with index %i and SSID %i had no "
                    "dependency\n", PC, index, inst_SSID);

            return 0;
        } else {
            DPRINTF(RxuStoreSet, "Inst %#x with index %i and SSID %i had LFST "
                    "inum of %i\n", PC, index, inst_SSID, inst_SSID);

            return inst_SSID;
        }
    }
}

void
StoreSet::issued(Addr issued_PC, InstSeqNum issued_seq_num, bool is_store)
{
    // auto start = std::chrono::high_resolution_clock::now();
    // This only is updated upon a store being issued.
    if (!is_store) {
        return;
    }

    int index = calcIndex(issued_PC);

    int store_SSID;

    assert(index < SSITSize);

    SeqNumMapIt store_list_it = storeList.find(issued_seq_num);

    if (store_list_it != storeList.end()) {
        storeList.erase(store_list_it);
    }

    // Make sure the SSIT still has a valid entry for the issued store.
    if (!validSSIT[index]) {
        return;
    }

    store_SSID = SSIT[index];

    assert(store_SSID < LFSTSize);

    // If the last fetched store in the store set refers to the store that
    // was just issued, then invalidate the entry.
    for (auto itvalid = validLFST[store_SSID].begin();itvalid != validLFST[store_SSID].end(); ++itvalid){
        if((*itvalid).second == true && (*itvalid).first == issued_seq_num){
            DPRINTF(RxuStoreSet, "StoreSet: store invalidated itself in LFST.seqnum :%i\n",issued_seq_num);
            (*itvalid).second = false;
        }
        if((*itvalid).first == issued_seq_num){
            validLFST[store_SSID].erase((*itvalid).first);
        }
    }

    // auto end = std::chrono::high_resolution_clock::now();
    // cprintf("Time taken by storeSet.checkInst: %llu\n",
    // std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());


    // if (validLFST[store_SSID] && LFST[store_SSID] == issued_seq_num) {
    //     DPRINTF(RxuStoreSet, "StoreSet: store invalidated itself in LFST.seqnum :%i\n",issued_seq_num);
    //     validLFST[store_SSID] = false;
    // }
}

void
StoreSet::squash(InstSeqNum squashed_num, ThreadID tid)
{
    DPRINTF(RxuStoreSet, "StoreSet: Squashing until inum %i\n",
            squashed_num);

    int idx;
    SeqNumMapIt store_list_it = storeList.begin();

    //@todo:Fix to only delete from correct thread
    while (!storeList.empty()) {
        idx = (*store_list_it).second;

        storeList.erase(store_list_it++);

        // bool younger = LFST[idx] > squashed_num;

        // if (validLFST[idx] && younger) {
        //     DPRINTF(RxuStoreSet, "Squashed [sn:%lli]\n", LFST[idx]);
        //     validLFST[idx] = false;

        //     storeList.erase(store_list_it++);
        // } else if (!validLFST[idx] && younger) {
        //     storeList.erase(store_list_it++);
        // }
    }
    if(!validLFST.empty()){
        for (auto it = validLFST.begin();it != validLFST.end(); ++it){
            auto itvalid = (*it).begin();
            if(!(*it).empty()){
                while (itvalid != (*it).end()) {
                    bool younger = (*itvalid).first > squashed_num;
                    if (younger) {
                        (*itvalid).second = false;
                        DPRINTF(RxuStoreSet, "StoreSet: Squashing (*itvalid).first:%i\n",
                        (*itvalid).first);
                        itvalid = (*it).erase(itvalid); // 删除当前元素，并让迭代器指向下一个元素
                    } else {
                        ++itvalid; // 指向下一个元素
                    }
                }
            }

        }
    }

}

void
StoreSet::clear()
{
    for (int i = 0; i < SSITSize; ++i) {
        validSSIT[i] = false;
    }

    for (int i = 0; i < LFSTSize; ++i) {
        //validLFST[i] = false;
        for (auto itvalid = validLFST[i].begin();itvalid != validLFST[i].end(); ++itvalid){
            (*itvalid).second = false;
        }
    }

    storeList.clear();
}

void
StoreSet::dump()
{
    cprintf("storeList.size(): %i\n", storeList.size());
    SeqNumMapIt store_list_it = storeList.begin();

    int num = 0;

    while (store_list_it != storeList.end()) {
        cprintf("%i: [sn:%lli] SSID:%i\n",
                num, (*store_list_it).first, (*store_list_it).second);
        num++;
        store_list_it++;
    }
}

} // namespace rxuo3
} // namespace gem5
