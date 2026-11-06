create_clock -name clk_core -period 5 [get_ports clk_core]
create_clock -name clk_ref -period 12 [get_ports clk_ref]
set_clock_groups -asynchronous -group [get_clocks clk_core] -group [get_clocks clk_ref]
