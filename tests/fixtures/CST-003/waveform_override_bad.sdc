create_clock -name root -period 10 [get_ports clk]
create_clock -name root -period 10 -waveform {1 6} [get_ports clk]
