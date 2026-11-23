create_clock -name src -period 7 [get_ports clk_s]
create_clock -name dst -period 13 [get_ports clk_d]
set_clock_groups -asynchronous -group [get_clocks src] -group [get_clocks dst]
