create_clock -name src -period 2 [get_ports sclk]
create_clock -name dst -period 13 [get_ports dclk]
set_clock_groups -asynchronous -group {src} -group {dst}
set_input_delay -clock src -max 0.2 [get_ports src_i]
set_input_delay -clock src -min 0 [get_ports src_i]
set_output_delay -clock dst -max 1 [get_ports out_o]
set_output_delay -clock dst -min 0 [get_ports out_o]
