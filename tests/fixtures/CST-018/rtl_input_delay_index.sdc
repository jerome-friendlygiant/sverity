create_clock -name clk -period 10 [get_ports clk]
create_clock -name launch -period 20
set_clock_groups -asynchronous -group {clk} -group {launch}
set_input_delay -clock launch 0 [get_ports {din[7]}]
