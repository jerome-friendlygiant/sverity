create_clock -name clk_src -period 9 [get_ports clk_src]
create_clock -name clk_dst -period 12 [get_ports clk_dst]
set_clock_groups -asynchronous -group [get_clocks clk_src] -group [get_clocks clk_dst]
