module top(input logic clk_a, clk_b, rst_n, input logic event_req, input logic [7:0] payload_in, output logic [7:0] out);
// event_req and payload_in are not constrained to hold or be acknowledged.
logic [7:0] p1,p2; logic e1,e2; always_ff @(posedge clk_b or negedge rst_n) if(!rst_n) begin p1<=0;p2<=0;e1<=0;e2<=0; end else begin p1<=payload_in;p2<=p1;e1<=event_req;e2<=e1; end
assign out={6'b0,e2,p2[0]}; endmodule
