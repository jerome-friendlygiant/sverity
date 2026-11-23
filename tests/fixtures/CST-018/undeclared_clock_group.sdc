create_clock -name clka -period 10 [get_ports clka]
set_clock_groups -asynchronous -group {clka} -group {missing_clk}
