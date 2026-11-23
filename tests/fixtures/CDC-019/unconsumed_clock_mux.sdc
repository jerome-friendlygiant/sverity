create_clock -name clk0_a -period 10 [get_ports clk0_a]
create_clock -name clk1_b -period 13 [get_ports clk1_b]
set_clock_groups -asynchronous -group {clk0_a} -group {clk1_b}
