module top(input logic clk_a, clk_b, rst_n, input logic [7:0] d, output logic [7:0] out);
// No source hold, encoding, or transaction contract is supplied.
logic [7:0] s1,s2; always_ff @(posedge clk_b or negedge rst_n) if(!rst_n) begin s1<=0;s2<=0; end else begin s1<=d;s2<=s1; end
assign out=s2; endmodule
