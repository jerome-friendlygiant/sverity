# Independent pointer premise check

`bench.sv` checks a complete 4-bit Gray sequence for one-bit changes and clocks the synchronizer with 8 ns/10 ns periods. It does not implement or prove FIFO memory, full/empty logic, reset startup, simultaneous read/write collision semantics, pointer wrap policy, physical inter-bit skew, or metastability resolution. Those remain integration and physical closure evidence.
