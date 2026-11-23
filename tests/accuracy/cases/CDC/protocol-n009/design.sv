module top(input logic clk_a, clk_b, rst_n, input logic pulse_a, output logic [7:0] out);
logic p1,p2; always_ff @(posedge clk_b or negedge rst_n) if(!rst_n) begin p1<=0;p2<=0; end else begin p1<=pulse_a;p2<=p1; end
assign out={7'b0,p2}; endmodule
