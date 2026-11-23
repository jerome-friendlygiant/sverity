create_clock -name clk -period 10 [get_ports clk]
create_clock -name external -period 10
set_input_delay -clock external 0 [get_ports e]
