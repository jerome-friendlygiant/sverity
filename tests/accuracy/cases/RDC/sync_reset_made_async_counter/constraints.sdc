create_clock -name clk_count -period 10 [get_ports clk_count]
create_clock -name clk_report -period 15 [get_ports clk_report]
set_clock_groups -asynchronous -group [get_clocks clk_count] -group [get_clocks clk_report]
