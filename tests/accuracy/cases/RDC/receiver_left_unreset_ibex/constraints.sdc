create_clock -name clk_src -period 10 [get_ports clk_src]
create_clock -name clk_dst -period 6 [get_ports clk_dst]
set_clock_groups -asynchronous -group [get_clocks clk_src] -group [get_clocks clk_dst]
