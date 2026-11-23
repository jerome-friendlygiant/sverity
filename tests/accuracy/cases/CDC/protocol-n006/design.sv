module top(input logic clk_a, clk_b, rst_n, input logic a,b, output logic [7:0] out);
logic [1:0] a1,a2,b1,b2; always_ff @(posedge clk_b or negedge rst_n) if(!rst_n) begin a1<=0;a2<=0;b1<=0;b2<=0; end else begin a1<=a;a2<=a1;b1<=b;b2<=b1; end
assign out={6'b0,a2,b2}; endmodule
