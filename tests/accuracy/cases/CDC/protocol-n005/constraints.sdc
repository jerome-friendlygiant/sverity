# rst_n is an asynchronous active-low reset; it has no synchronous I/O delay contract.
create_clock -name clk_a -period 10 [get_ports {clk_a}]
create_clock -name clk_b -period 13 [get_ports {clk_b}]
set_clock_groups -asynchronous -group [get_clocks {clk_a}] -group [get_clocks {clk_b}]
set_input_delay -clock clk_a 1.0 [get_ports {d}]
set_output_delay -clock clk_b 1.0 [get_ports {out}]
