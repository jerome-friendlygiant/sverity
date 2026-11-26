create_clock -name core -period 10 [get_ports clk_core]
create_clock -name link -period 8 [get_ports clk_link]
set_clock_groups -asynchronous -group [get_clocks core] -group [get_clocks link]
set_input_delay -clock link -max 2 [get_ports link_up_i]
