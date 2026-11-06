create_clock -name clk -period 10 [get_ports clk]
set_input_delay 2 -clock clk [get_ports serial_i]
set_output_delay 2 -clock clk [get_ports {rising_o falling_o serial_o}]
