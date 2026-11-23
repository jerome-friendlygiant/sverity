create_clock -name clk -period 10 [get_ports clk]
create_clock -name external -period 7
set_clock_groups -asynchronous -group {clk} -group {external}
set_input_delay -clock clk 0 [get_ports e]
set_input_delay -clock external not_a_num [get_ports e]
