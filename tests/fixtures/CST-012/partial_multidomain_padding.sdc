create_clock -name a -period 10 [get_ports a]
create_clock -name b -period 11 [get_ports b]
set_clock_groups -asynchronous -group a -group b
set_output_delay -clock a 0 [get_ports {q[2]}]
set_output_delay -clock b 0 [get_ports {q[1]}]
