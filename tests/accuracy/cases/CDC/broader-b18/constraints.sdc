# Authored pin timing contract; Gray-pointer skew and reset assumptions are in AUTHORED_READINGS.md.

create_clock -name wclk -period 10.000 [get_ports {wclk}]
create_clock -name rclk -period 10.000 [get_ports {rclk}]
set_clock_groups -asynchronous -group [get_clocks {wclk}] -group [get_clocks {rclk}]

set_input_delay -clock wclk -max 1.500 [get_ports {rst_n}]
set_input_delay -clock wclk -min 0.100 [get_ports {rst_n}]
set_input_delay -clock rclk -max 1.500 [get_ports {rst_n}]
set_input_delay -clock rclk -min 0.100 [get_ports {rst_n}]
set_input_delay -clock wclk -max 1.500 [get_ports {wpush}]
set_input_delay -clock wclk -min 0.100 [get_ports {wpush}]
set_input_delay -clock rclk -max 1.500 [get_ports {rpop}]
set_input_delay -clock rclk -min 0.100 [get_ports {rpop}]
set_output_delay -clock rclk -max 1.500 [get_ports {wgray_seen}]
set_output_delay -clock rclk -min 0.100 [get_ports {wgray_seen}]
set_output_delay -clock wclk -max 1.500 [get_ports {rgray_seen}]
set_output_delay -clock wclk -min 0.100 [get_ports {rgray_seen}]
