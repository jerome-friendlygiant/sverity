# Authored pin timing contract; asynchronous functional assumptions are in AUTHORED_READINGS.md.

create_clock -name src_clk -period 10.000 [get_ports {src_clk}]
create_clock -name dst_clk -period 10.000 [get_ports {dst_clk}]
set_clock_groups -asynchronous -group [get_clocks {src_clk}] -group [get_clocks {dst_clk}]

set_input_delay -clock dst_clk -max 1.500 [get_ports {rst_n}]
set_input_delay -clock dst_clk -min 0.100 [get_ports {rst_n}]
set_input_delay -clock src_clk -max 1.500 [get_ports {rst_n}]
set_input_delay -clock src_clk -min 0.100 [get_ports {rst_n}]
set_input_delay -clock src_clk -max 1.500 [get_ports {req}]
set_input_delay -clock src_clk -min 0.100 [get_ports {req}]
set_output_delay -clock dst_clk -max 1.500 [get_ports {seen}]
set_output_delay -clock dst_clk -min 0.100 [get_ports {seen}]
