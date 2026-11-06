create_clock -name clk_wr -period 8 [get_ports clk_wr]
create_clock -name clk_rd -period 11 [get_ports clk_rd]
set_clock_groups -asynchronous -group [get_clocks clk_wr] -group [get_clocks clk_rd]
