create_clock -name clk0_a -period 10.0 [get_ports clk0_a]
create_clock -name clk1_b -period 13.0 [get_ports clk1_b]
set_clock_groups -asynchronous -group [get_clocks clk0_a] -group [get_clocks clk1_b]
