# Copyright (c) 2025 The Author
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""CXL Universal memory system for gem5"""

from typing import (
    List,
    Sequence,
    Tuple,
)

from m5.objects import (
    AddrRange,
    CXLUniversal,
    MemCtrl,
    MemInterface,
    Port,
)
from m5.util.convert import toMemorySize

from ...utils.override import overrides
from ..boards.abstract_board import AbstractBoard
from .abstract_memory_system import AbstractMemorySystem


class CXLUniversalMemory(AbstractMemorySystem):
    """
    A CXL Universal memory system for gem5.

    This class wraps the CXLUniversal C++ memory controller and provides
    the necessary interface for gem5's memory system framework.
    """

    def __init__(
        self,
        config_file: str,
        result_file: str,
        size: str,
        range_start: int = 0,
    ) -> None:
        """
        Initialize the CXL Universal memory system.

        :param config_file: Path to CXL Universal configuration file
        :param result_file: Path to result output file
        :param size: Memory size (e.g., "4GB")
        :param range_start: Starting address for memory range
        """
        super().__init__()

        self.cxl_universal = CXLUniversal(
            configFile=config_file,
            filePath=result_file,
            range=AddrRange(range_start, range_start + toMemorySize(size) - 1),
        )

        self._size = toMemorySize(size)
        self._range_start = range_start

    @overrides(AbstractMemorySystem)
    def incorporate_memory(self, board: AbstractBoard) -> None:
        """Incorporate this memory system into the board."""
        # No special incorporation needed for CXL Universal
        pass

    @overrides(AbstractMemorySystem)
    def get_mem_ports(self) -> Sequence[Tuple[AddrRange, Port]]:
        """Get the memory ports for this memory system."""
        return [(self.cxl_universal.range, self.cxl_universal.port)]

    @overrides(AbstractMemorySystem)
    def get_memory_controllers(self) -> List[MemCtrl]:
        """Get all memory controllers in this memory system."""
        return [self.cxl_universal]

    @overrides(AbstractMemorySystem)
    def get_mem_interfaces(self) -> List[MemInterface]:
        """Get all memory interfaces in this memory system."""
        # CXLUniversal doesn't use traditional MemInterface objects
        return []

    @overrides(AbstractMemorySystem)
    def get_size(self) -> int:
        """Returns the total size of the memory system."""
        return self._size

    @overrides(AbstractMemorySystem)
    def set_memory_range(self, ranges: List[AddrRange]) -> None:
        """Set the memory range for this memory system."""
        if len(ranges) != 1:
            raise Exception(
                "CXL Universal memory controller requires exactly one "
                "address range."
            )

        range_obj = ranges[0]
        if range_obj.size() != self._size:
            raise Exception(
                f"CXL Universal memory controller requires a range "
                f"which matches the memory's size.\n"
                f"Range size: {range_obj.size()}\n"
                f"Memory size: {self._size}"
            )

        self.cxl_universal.range = range_obj

    @overrides(AbstractMemorySystem)
    def get_uninterleaved_range(self) -> List[AddrRange]:
        """Get the uninterleaved memory range."""
        return [self.cxl_universal.range]
