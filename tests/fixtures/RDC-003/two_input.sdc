create_clock -name clk -period 10 [get_ports clk]
set_input_delay 2 -clock clk [get_ports {d_i en_i rst2_ni}]
