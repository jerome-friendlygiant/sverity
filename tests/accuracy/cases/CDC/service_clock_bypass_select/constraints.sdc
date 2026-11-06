create_clock -name run -period 8 [get_ports run_clk]
create_clock -name service -period 25 [get_ports service_clk]
set_clock_groups -asynchronous -group [get_clocks run] -group [get_clocks service]
