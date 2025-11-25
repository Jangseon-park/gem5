#include "mem/cxluniversal.hh"

#include <gem5/cxl_u_wrapper.h>

#include "base/addr_range.hh"
#include "base/callback.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "debug/CXLUniversal.hh"
#include "mem/packet.hh"
#include "sim/cur_tick.hh"
#include "sim/sim_exit.hh"
#include "sim/system.hh"

using std::bind;
namespace gem5
{
namespace memory
{

CXLUniversal::CXL_U_port::CXL_U_port(const std::string &_name,
                                       CXLUniversal &_mem)
    : ResponsePort(_name), mem(_mem)
{
}

Tick CXLUniversal::CXL_U_port::recvAtomic(PacketPtr pkt)
{
    return mem.recv_atomic(pkt);
}

void CXLUniversal::CXL_U_port::recvFunctional(PacketPtr pkt)
{
    mem.recv_functional(pkt);
}

bool CXLUniversal::CXL_U_port::recvTimingReq(PacketPtr pkt)
{
    return mem.recv_timing_req(pkt);
}

void CXLUniversal::CXL_U_port::recvRespRetry()
{
    mem.recv_resp_retry();
}

AddrRangeList CXLUniversal::CXL_U_port::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(mem.getAddrRange());
    return ranges;
}

CXLUniversal::CXLUniversal(const Params &p) :
    memory::AbstractMemory(p),
    port(name() + ".port", *this),
    config_file_path(p.configFile),
    result_file_path(p.filePath),
    read_callback(std::bind(&CXLUniversal::read_complete, this,
                            std::placeholders::_1, std::placeholders::_2)),
    write_callback(std::bind(&CXLUniversal::write_complete, this,
                             std::placeholders::_1, std::placeholders::_2)),
    req_retry_callback(std::bind(&CXLUniversal::recv_retry_for_host_bridge,
                                 this, std::placeholders::_1,
                                 std::placeholders::_2)),
    wrapper(std::make_shared<::CXLUniv::CXLUnivWrapper>(
        config_file_path, result_file_path, read_callback, write_callback,
        req_retry_callback)),
    start_tick(0),
    resp_stall(false),
    req_stall(false),
    send_resp_event([this] { process_send_resp(); }, name()),
    tick_event([this] { process_tick(); }, name())
{
    DPRINTF(CXLUniversal, "CXLUniversal constructor\n");
    DPRINTF(CXLUniversal, "config_file_path: %s\n", config_file_path.c_str());
    DPRINTF(CXLUniversal, "result_file_path: %s\n", result_file_path.c_str());
    registerExitCallback([this]() { wrapper->finish(); });
}

CXLUniversal::~CXLUniversal()
{
    // std::shared_ptr automatically cleans up
}

void CXLUniversal::init()
{
    AbstractMemory::init();
    wrapper->initialize();
    if (!port.isConnected()) {
        fatal("CXLUniversal %s is unconnected!\n", name());
    } else {
        port.sendRangeChange();
    }
}

void CXLUniversal::startup()
{
    DPRINTF(CXLUniversal, "CXLUniversal::startup\n");
    start_tick = curTick();
    //schedule(tick_event, clockEdge());
}

DrainState CXLUniversal::drain()
{
    DPRINTF(CXLUniversal, "CXLUniversal::drain\n");
    // Check if any path has pending operations
    if (wrapper->is_pending(::CXLUniv::PathType::INPUT) ||
        wrapper->is_pending(::CXLUniv::PathType::OUTPUT)) {
        return DrainState::Draining;
    }
    return DrainState::Drained;
}

Port &CXLUniversal::getPort(const std::string &if_name, PortID idx)
{
    DPRINTF(CXLUniversal, "CXLUniversal::getPort\n");
    if (if_name != "port") {
        return ClockedObject::getPort(if_name, idx);
    } else {
        return port;
    }
}

Tick CXLUniversal::recv_atomic(PacketPtr pkt)
{
    DPRINTF(CXLUniversal, "CXLUniversal::recv_atomic\n");
    access(pkt);
    return pkt->cacheResponding() ? 0 : 100000; // 100 ns
}

void CXLUniversal::recv_functional(PacketPtr pkt)
{
    DPRINTF(CXLUniversal, "CXLUniversal::recv_functional\n");
    pkt->pushLabel(name());
    functionalAccess(pkt);
    for (auto i = response_queue.begin(); i != response_queue.end(); ++i)
        pkt->trySatisfyFunctional(*i);
    pkt->popLabel();
}

bool CXLUniversal::recv_timing_req(PacketPtr pkt)
{
    DPRINTF(CXLUniversal, "CXLUniversal::recv_timing_req\n");
    if (pkt->cacheResponding()) {
        pendingDelete.reset(pkt);
        return true;
    }
    if (req_stall) return false;
    if (wrapper->is_full(::CXLUniv::PathType::INPUT)) {
        req_stall = true;
        return false;
    }
    if (pkt->isRead()) {
        DPRINTF(CXLUniversal, "CXLUniversal::recv_timing_req: Read\n");
        inflight_read_req++;
        inflight_read_req_queue[pkt->getAddr()].push(pkt);
        wrapper->recv_from_gem5(curTick(),
                                pkt->getAddr() - getAddrRange().start(),
                                0); // Read
        if (!tick_event.scheduled()) {
            schedule(tick_event, clockEdge());
        }
        return true;
    } else if (pkt->isWrite()) {
        DPRINTF(CXLUniversal, "CXLUniversal::recv_timing_req: Write\n");
        inflight_write_req++;
        inflight_write_req_queue[pkt->getAddr()].push(pkt);
        wrapper->recv_from_gem5(curTick(),
                                pkt->getAddr() - getAddrRange().start(),
                                1); // Write
        access_and_respond(pkt);
        if (!tick_event.scheduled()) {
            schedule(tick_event, clockEdge());
        }
        return true;
    } else {
        access_and_respond(pkt);
        return true;
    }
}

void CXLUniversal::recv_resp_retry()
{
    assert(resp_stall);
    resp_stall = false;
    process_send_resp();
}

void CXLUniversal::access_and_respond(PacketPtr pkt)
{
    bool need_resp = pkt->needsResponse();
    access(pkt);
    if (need_resp) {
        assert(pkt->isResponse());
        Tick time = curTick() + pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = 0;
        pkt->payloadDelay = 0;
        response_queue.push_back(pkt);
        if (!resp_stall && !send_resp_event.scheduled()) {
            schedule(send_resp_event, time);
        }
    } else {
        pendingDelete.reset(pkt);
    }
}

uint64_t CXLUniversal::get_size() const
{
    return wrapper->get_size();
}

void CXLUniversal::read_complete(uint64_t address, uint64_t when)
{
    DPRINTF(CXLUniversal, "CXLUniversal::read_complete\n");
    uint64_t global_addr = address + getAddrRange().start();
    auto p = inflight_read_req_queue.find(global_addr);
    assert(p != inflight_read_req_queue.end());
    PacketPtr pkt = p->second.front();
    p->second.pop();
    if (p->second.empty()) inflight_read_req_queue.erase(p);
    assert(inflight_read_req != 0);
    --inflight_read_req;
    access_and_respond(pkt);
}

void CXLUniversal::write_complete(uint64_t address, uint64_t when)
{
    DPRINTF(CXLUniversal, "CXLUniversal::write_complete\n");
    uint64_t global_addr = address + getAddrRange().start();
    auto p = inflight_write_req_queue.find(global_addr);
    assert(p != inflight_write_req_queue.end());
    p->second.pop();
    if (p->second.empty()) inflight_write_req_queue.erase(p);
    assert(inflight_write_req != 0);
    --inflight_write_req;
    if (!wrapper->is_pending(::CXLUniv::PathType::INPUT) &&
        !wrapper->is_pending(::CXLUniv::PathType::OUTPUT) &&
        inflight_read_req == 0 && inflight_write_req == 0) {
        signalDrainDone();
    }
}

void CXLUniversal::recv_retry_for_host_bridge(uint64_t address, uint64_t when)
{
    DPRINTF(CXLUniversal, "CXLUniversal::recv_retry_for_host_bridge\n");
    if (req_stall && !wrapper->is_full(::CXLUniv::PathType::INPUT)) {
        req_stall = false;
        port.sendRetryReq();
    }
}

void CXLUniversal::process_send_resp()
{
    assert(!resp_stall);
    assert(!response_queue.empty());
    bool success = port.sendTimingResp(response_queue.front());
    if (success) {
        response_queue.pop_front();
        if (!response_queue.empty() && !send_resp_event.scheduled()) {
            schedule(send_resp_event, curTick());
        if (inflight_read_req == 0 && inflight_write_req == 0 &&
            !wrapper->is_pending(::CXLUniv::PathType::INPUT) &&
            !wrapper->is_pending(::CXLUniv::PathType::OUTPUT)) {
                signalDrainDone();
            }
        }
    } else {
        resp_stall = true;
        assert(!send_resp_event.scheduled());
    }
}

void CXLUniversal::process_tick()
{
    DPRINTF(CXLUniversal, "CXLUniversal::process_tick\n");
    // Always tick the wrapper (no isTimingMode check needed)
    if (system()->isTimingMode()) {
        DPRINTF(CXLUniversal, "CXLUniversal::process_tick: timing mode\n");
        wrapper->tick(curTick());
        if (inflight_read_req != 0 || inflight_write_req != 0) {
            Tick next_tick = wrapper->get_next_tick();
            if (next_tick == 0) {
                next_tick = curTick() +
                    wrapper->get_picosec_per_tick() * sim_clock::as_int::ps;
            }
            DPRINTF(CXLUniversal,
                    "CXLUniversal::process_tick: next_tick=%lu\n", next_tick);
            if (tick_event.scheduled()) {
                reschedule(tick_event, next_tick);
            } else {
                schedule(tick_event, next_tick);
            }
        }
    } else {
        DPRINTF(CXLUniversal, "CXLUniversal::process_tick: not timing mode\n");
    }
}

} // namespace memory
} // namespace gem5
