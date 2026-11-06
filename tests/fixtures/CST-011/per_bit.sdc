create_clock -name core -period 10 [get_ports clk]
set_input_delay -clock core 1 [get_ports {din[0]}]
set_input_delay -clock core 1 [get_ports {din[1]}]
