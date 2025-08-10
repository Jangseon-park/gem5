#!/usr/bin/env python3

import argparse
import os

import m5
import m5.stats as m5stats
from m5.objects import (
    System,
    SrcClockDomain,
    VoltageDomain,
    TimingSimpleCPU,
    AddrRange,
    SystemXBar,
    SimpleMemory,
    Root,
    SEWorkload,
    Process,
)


def build_system(binary_path: str, page_fault_latency: int, mem_size: str):
    system = System()
    system.clk_domain = SrcClockDomain()
    system.clk_domain.clock = "1GHz"
    system.clk_domain.voltage_domain = VoltageDomain()

    system.mem_mode = "timing"
    system.mem_ranges = [AddrRange(mem_size)]

    system.cpu = TimingSimpleCPU(page_fault_latency=page_fault_latency)

    system.membus = SystemXBar()

    # Simple memory controller
    system.mem = SimpleMemory(range=system.mem_ranges[0], latency="50ns")
    system.mem.port = system.membus.mem_side_ports

    # Connect CPU I/D ports
    system.cpu.icache_port = system.membus.cpu_side_ports
    system.cpu.dcache_port = system.membus.cpu_side_ports

    # System port for functional accesses
    system.system_port = system.membus.cpu_side_ports

    # SE workload
    workload = SEWorkload.init_compatible(binary_path)
    system.workload = workload

    process = Process()
    process.executable = binary_path
    process.cmd = [binary_path]
    system.cpu.workload = process
    system.cpu.createThreads()

    return system


def main():
    parser = argparse.ArgumentParser(
        description=(
            "TimingSimpleCPU page-fault-latency smoke test (SE mode). "
            "Runs a user binary with TimingSimpleCPU and prints stats."
        )
    )
    parser.add_argument(
        "--binary",
        type=str,
        required=False,
        default="tests/cxl_u/pf_storm",
        help=(
            "Path to a user-mode binary (default: tests/cxl_u/pf_storm). "
            "Build with: (cd tests/cxl_u && make)"
        ),
    )
    parser.add_argument(
        "--page-fault-latency",
        type=int,
        default=0,
        help="Cycles to stall after a page fault (default: 0)",
    )
    parser.add_argument(
        "--mem-size", type=str, default="512MB", help="Memory size (default: 512MB)"
    )
    parser.add_argument(
        "--max-ticks",
        type=int,
        default=0,
        help=(
            "Optional: stop after this many ticks (0 means run until program exit)"
        ),
    )

    args = parser.parse_args()

    if not os.path.isfile(args.binary):
        raise RuntimeError(
            f"Binary not found: {args.binary}. Try building: cd tests/cxl_u && make"
        )

    system = build_system(args.binary, args.page_fault_latency, args.mem_size)
    root = Root(full_system=False, system=system)
    m5.instantiate()

    if args.max_ticks and args.max_ticks > 0:
        exit_event = m5.simulate(args.max_ticks)
    else:
        exit_event = m5.simulate()

    print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")

    # Ensure stats are dumped to m5out/stats.txt
    try:
        m5stats.dump()
        print("Stats dumped to m5out/stats.txt")
    except Exception as e:
        print(f"Failed to dump stats: {e}")

    # Print a small subset of stats of interest to stdout.
    # Full stats are written to m5out/stats.txt by gem5 as usual.
    try:
        # Access C++ stat names
        cpu_name = system.cpu.name()
        # The names match those registered in TimingSimpleCPU::regStats()
        print("\nKey stats (also see m5out/stats.txt):")
        print(f"  {cpu_name}.numPageFaultStalls: (see stats.txt)")
        print(f"  {cpu_name}.totalPageFaultStallCycles: (see stats.txt)")
    except Exception:
        pass


if __name__ == "__main__":
    main()


