#ifndef CXL_UNIVERSAL_GEM5_H
#define CXL_UNIVERSAL_GEM5_H

#include <deque>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>

#include "base/addr_range.hh"
#include "base/types.hh"
#include "gem5/cxl_u_wrapper.h"
#include "mem/abstract_mem.hh"
#include "mem/packet.hh"
#include "mem/qport.hh"
#include "params/CXLUniversal.hh"
#include "sim/eventq.hh"

namespace gem5
{

namespace memory
{

class CXLUniversal : public AbstractMemory
{
  private:
    class CXL_U_port : public ResponsePort
    {
      public:
        CXL_U_port(const std::string &_name, CXLUniversal &_mem);

      private:
        CXLUniversal &mem;

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
        AddrRangeList getAddrRanges() const override;
    };
    CXL_U_port port;

    std::string config_file_path;
    std::string result_file_path;
    std::function<void(uint64_t, uint64_t)> read_callback;
    std::function<void(uint64_t, uint64_t)> write_callback;
    std::function<void(uint64_t, uint64_t)> req_retry_callback;
    std::shared_ptr<::CXLUniv::CXLUnivWrapper> wrapper;
    Tick start_tick;
    bool resp_stall;
    bool req_stall;
    Tick end_tick;
    uint64_t num_read_req = 0;
    uint64_t num_write_req = 0;

    uint64_t inflight_read_req = 0;
    uint64_t inflight_write_req = 0;
    std::unordered_map<uint64_t, std::queue<PacketPtr>>
        inflight_read_req_queue;
    std::unordered_map<uint64_t, std::queue<PacketPtr>>
        inflight_write_req_queue;
    std::deque<PacketPtr> response_queue;
    std::unique_ptr<Packet> pendingDelete;

    EventFunctionWrapper send_resp_event;
    void process_send_resp();
    EventFunctionWrapper tick_event;
    void process_tick();

  public:
    typedef CXLUniversalParams Params;
    CXLUniversal(const Params &p);

    void init() override;
    void startup() override;
    DrainState drain() override;
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    virtual ~CXLUniversal();
    uint64_t get_size() const;

  protected:
    Tick recv_atomic(PacketPtr pkt);
    void recv_functional(PacketPtr pkt);
    bool recv_timing_req(PacketPtr pkt);
    void recv_resp_retry();
    void access_and_respond(PacketPtr pkt);

    void read_complete(uint64_t address, uint64_t when);
    void write_complete(uint64_t address, uint64_t when);
    void recv_retry_for_host_bridge(uint64_t address, uint64_t when);
};

} // namespace memory

} // namespace gem5

#endif // CXL_UNIVERSAL_GEM5_H
