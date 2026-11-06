create_clock -name clk_src -period 10.0 [get_ports clk_src_i]
create_clock -name clk_dst -period 7.0 [get_ports clk_dst_i]
set_clock_groups -asynchronous \
  -group [get_clocks clk_src] \
  -group [get_clocks clk_dst]
