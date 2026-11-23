module top(input logic clk_a, clk_b, rst_n, input logic event_toggle, input logic [7:0] payload, output logic [7:0] out);
logic t1,t2; logic [7:0] p1,p2; always_ff @(posedge clk_b or negedge rst_n) if(!rst_n) begin t1<=0;t2<=0;p1<=0;p2<=0; end else begin t1<=event_toggle;t2<=t1;p1<=payload;p2<=p1; end
assign out={6'b0,t2,p2[0]}; endmodule
