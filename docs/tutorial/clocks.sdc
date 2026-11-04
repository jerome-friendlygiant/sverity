create_clock -name a -period 10 [get_ports a]
create_clock -name b -period 13 [get_ports b]
set_clock_groups -asynchronous -group [get_clocks a] -group [get_clocks b]
