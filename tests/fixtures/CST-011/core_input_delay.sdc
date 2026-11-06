create_clock -name core -period 10 [get_ports clk]
set_input_delay -clock core 2 [get_ports din]
