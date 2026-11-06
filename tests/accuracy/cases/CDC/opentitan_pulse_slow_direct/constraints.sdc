create_clock -name clk_fast -period 5.0 [get_ports clk_fast]
create_clock -name clk_slow -period 20.0 [get_ports clk_slow]
set_clock_groups -asynchronous -group [get_clocks clk_fast] -group [get_clocks clk_slow]
