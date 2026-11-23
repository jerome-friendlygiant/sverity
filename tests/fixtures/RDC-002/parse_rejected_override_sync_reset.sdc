create_clock -name core -period 10 [get_ports clk]
set_input_delay -clock core 2 [get_ports clear]
set_input_delay -clock core not_a_num [get_ports clear]
