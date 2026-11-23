create_clock -name A -period 8 [get_ports clk]
create_clock -name B -period 8 [get_ports B]
set_clock_groups -asynchronous -group A -group [get_ports B]
set_input_delay -clock A 0 [get_ports d]
