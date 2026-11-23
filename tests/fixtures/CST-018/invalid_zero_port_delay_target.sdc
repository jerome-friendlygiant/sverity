set_units -time ns
create_clock -name clk -period 10 [get_ports clk]
set_input_delay -clock clk 0 [get_ports {d nonexistent}]
