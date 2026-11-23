create_clock -name a -period 10 [get_ports clk]
set_input_delay -clock a 0 [get_ports d]
set_false_path -from [get_cells g] -to [get_ports q]
