create_clock -name src -period 4 [get_ports clk_s]
create_clock -name dst -period 20 [get_ports clk_d]
set_clock_groups -asynchronous -group [get_clocks src] -group [get_clocks dst]
