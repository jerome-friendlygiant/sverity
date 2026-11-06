create_clock -name clk -period 10 [get_ports clk]
set_input_delay 2 -clock clk [get_ports {rst in_valid in_data out_ready}]
set_output_delay 2 -clock clk [get_ports {in_ready out_valid out_data}]
