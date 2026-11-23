# Authored pin timing contract; asynchronous functional assumptions are in AUTHORED_READINGS.md.

create_clock -name clk -period 10.000 [get_ports {clk}]

set_input_delay -clock clk -max 1.500 [get_ports {rst_n}]
set_input_delay -clock clk -min 0.100 [get_ports {rst_n}]
set_input_delay -clock clk -max 1.500 [get_ports {en}]
set_input_delay -clock clk -min 0.100 [get_ports {en}]
set_input_delay -clock clk -max 1.500 [get_ports {d}]
set_input_delay -clock clk -min 0.100 [get_ports {d}]
set_output_delay -clock clk -max 1.500 [get_ports {q}]
set_output_delay -clock clk -min 0.100 [get_ports {q}]
