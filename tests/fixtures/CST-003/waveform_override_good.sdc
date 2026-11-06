create_clock -name root -period 10 [get_ports clk]
create_clock -name root -period 10 -waveform {0 5} [get_ports clk]
