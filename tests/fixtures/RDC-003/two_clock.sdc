create_clock -name clk -period 10 [get_ports clk]
create_clock -name clk2 -period 7 [get_ports clk2]
set_clock_groups -asynchronous -group {clk} -group {clk2}
set_input_delay 2 -clock clk [get_ports {d_i en_i}]
