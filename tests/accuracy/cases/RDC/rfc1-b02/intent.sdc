create_clock -name clk_a -period 10 [get_ports clk_a]
create_clock -name clk_b -period 7 [get_ports clk_b]
set_clock_groups -asynchronous -group [get_clocks clk_a] -group [get_clocks clk_b]
set_input_delay -clock clk_a -max 2 -min 0 [get_ports run_i]
