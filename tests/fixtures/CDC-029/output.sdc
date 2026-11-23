create_clock -name clkb -period 7 [get_ports clkb]
set_output_delay 1 -clock clkb [get_ports flag_out]
