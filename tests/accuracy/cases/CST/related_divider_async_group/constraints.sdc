create_clock -name clk_i -period 10.000 [get_ports clk_i]
create_generated_clock -name div_clk -source [get_ports clk_i] -divide_by 4 [get_ports div_clk]
set_clock_groups -asynchronous -group [get_clocks clk_i] -group [get_clocks div_clk]
