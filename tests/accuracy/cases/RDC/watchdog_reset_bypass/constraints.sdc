create_clock -name clk_i -period 10 [get_ports clk_i]
set_input_delay 2 -clock clk_i [get_ports command_i]
set_output_delay 2 -clock clk_i [get_ports status_o]
set_false_path -from [get_ports {por_ni watchdog_trip_i}]
