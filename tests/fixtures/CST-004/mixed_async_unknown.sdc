create_clock -name clk -period 10 [get_ports clk]
create_clock -name ext -period 7
create_clock -name vclk -period 13
set_clock_groups -asynchronous -group {clk} -group {ext}
set_input_delay -clock ext -min 0 [get_ports e]
set_input_delay -clock vclk -max 1 [get_ports e]
