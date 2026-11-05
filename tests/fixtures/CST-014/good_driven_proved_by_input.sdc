create_clock -name a -period 10 [get_ports a]
create_clock -name b -period 7 [get_ports b]
set_clock_groups -asynchronous -group a -group b
set_case_analysis 0 [get_ports en]
set_case_analysis 0 [get_nets top.m]
