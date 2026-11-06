create_clock -name clk -period 10 [get_ports clk]
set_input_delay 2 -clock clk [get_ports {clear_i in_valid_i in_rdata_i out_ready_i}]
set_output_delay 2 -clock clk [get_ports {out_valid_o out_rdata_o}]
