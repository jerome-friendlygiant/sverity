create_clock -name clka -period 10 [get_ports clka]
set_input_delay -clock clka 1.0 [get_ports rst_n]
