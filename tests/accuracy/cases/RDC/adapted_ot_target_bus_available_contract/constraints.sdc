create_clock -name clk_i -period 10 [get_ports clk_i]
set_input_delay 2 -clock clk_i [get_ports {enable_i sw_reset_i recovery_i bus_avail_i}]
set_output_delay 2 -clock clk_i [get_ports {reset_trig_o inbuf_en_o}]
