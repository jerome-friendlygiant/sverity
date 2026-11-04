create_clock -name clk_a -period 10 [get_ports clk_a]
create_clock -name clk_b -period 7 [get_ports clk_b]
set_clock_groups -asynchronous -group clk_a -group clk_b
set_case_analysis 0 [get_nets top.mode]
