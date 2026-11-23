create_clock -name a -period 10 [get_ports clk]
create_clock -name b -period 11 [get_ports alt]
set_clock_groups -asynchronous -group a -group b
set_input_delay -clock a 0 [get_ports d]
set_false_path -from [get_cells {a_q[0]}] -to [get_cells q]
