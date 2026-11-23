# CDC-RFC1-T13 interface contract

rst_n assertion is asynchronous and may occur any time. Deassertion can occur arbitrarily close to dst_clk; integration requires locally synchronized release and recovery/removal closure.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.
