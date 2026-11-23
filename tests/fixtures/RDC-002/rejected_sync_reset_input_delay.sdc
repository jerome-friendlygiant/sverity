create_clock -name core -period 10 [get_ports clk]
set_input_delay -clock bogus 1 [get_ports {clear nothere}]
