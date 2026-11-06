create_clock -name clk_main -period 10 [get_ports clk_main]
create_clock -name clk_aux -period 7 [get_ports clk_aux]
set_clock_groups -asynchronous -group [get_clocks clk_main] -group [get_clocks clk_aux]
