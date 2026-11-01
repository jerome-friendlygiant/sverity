create_clock -name clka -period 10 [get_ports clka]
set_output_delay 2.0 -clock clka [get_ports d]
