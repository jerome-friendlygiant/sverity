create_clock -name clk_a -period 10.0 [get_ports clk_a]
create_clock -name clk_src -period 7.5 [get_ports clk_src]
set_clock_groups -asynchronous -group [get_clocks clk_a] -group [get_clocks clk_src]
set_input_delay -clock clk_a 2.0 [get_ports en_i]
