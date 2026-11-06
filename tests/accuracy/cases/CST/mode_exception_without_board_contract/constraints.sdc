create_clock -name core -period 10 [get_ports clk]
set_case_analysis 0 [get_ports mode]
set_input_delay -clock core 2 [get_ports *data]
set_output_delay -clock core 2 [get_ports dout*]
