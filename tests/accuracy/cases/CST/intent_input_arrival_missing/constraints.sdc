create_clock -name core -period 10 [get_ports clk]
set_output_delay -clock core -min 0 [get_ports q]
set_output_delay -clock core -max 2 [get_ports q]
