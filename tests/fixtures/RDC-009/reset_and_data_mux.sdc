create_clock -name clk_core -period 10 [get_ports clk_core]
create_clock -name clk_link -period 7 [get_ports clk_link]
set_clock_groups -asynchronous -group {clk_core} -group {clk_link}
