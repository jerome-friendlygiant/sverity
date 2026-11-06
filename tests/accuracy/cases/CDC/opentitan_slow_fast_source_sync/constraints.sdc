create_clock -name clk_slow -period 20.0 [get_ports clk_slow]
create_clock -name clk_fast -period 4.0 [get_ports clk_fast]
set_clock_groups -asynchronous -group [get_clocks clk_slow] -group [get_clocks clk_fast]
