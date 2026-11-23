module tb;
reg clk_a=0, clk_b=1, rst_n=0; reg [3:0] data_i=0; reg event_i=0, mode_i=0; wire [3:0] data_o; wire event_o;
always #5 clk_a=~clk_a; always #5 clk_b=~clk_b;
top dut(.clk_a(clk_a),.clk_b(clk_b),.rst_n(rst_n),.data_i(data_i),.event_i(event_i),.mode_i(mode_i),.data_o(data_o),.event_o(event_o));
initial begin #12 rst_n=1; data_i=4'h5; #4 event_i=1; #2 event_i=0; #40 $finish; end
endmodule
