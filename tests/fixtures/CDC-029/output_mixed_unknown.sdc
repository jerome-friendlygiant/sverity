create_clock -name launch_a -period 8 [get_ports launch_a]
create_clock -name capture_b -period 10 [get_ports capture_b]
create_clock -name unrelated_c -period 12 [get_ports unrelated_c]
set_clock_groups -asynchronous -group {launch_a} -group {capture_b}
set_input_delay -min 1 -clock unrelated_c [get_ports data]
set_input_delay -add_delay -min 1 -clock launch_a [get_ports data]
set_output_delay 1 -clock capture_b [get_ports result]
