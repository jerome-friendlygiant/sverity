# CDC-RFC1-T19 interface contract

`src_a` is a changing source-domain status level. `dst_mode` is synchronous to `dst_clk`; the output combines one asynchronous influence after two-stage synchronization with one local mode control. The consumer tolerates normal status latency.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.
