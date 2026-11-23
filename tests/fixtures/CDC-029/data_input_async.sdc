create_clock -name src -period 10 [get_ports src_clk]
create_clock -name dst -period 12 [get_ports dst_clk]
set_clock_groups -asynchronous -group {src} -group {dst}
set_input_delay -clock src 1 [get_ports data]
