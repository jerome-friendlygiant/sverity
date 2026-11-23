module top(input logic clk_a, clk_b, rst_n, input logic [7:0] d, output logic [7:0] out);
logic [7:0] q; always_ff @(posedge clk_a or negedge rst_n) if(!rst_n) q<=0; else q<=d;
assign out=q; endmodule
