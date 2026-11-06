create_clock -name core -period 10 [get_ports core_clock]
set_input_delay -clock core 2 [get_ports din*]
set_output_delay -clock core 2 [get_ports dout*]
