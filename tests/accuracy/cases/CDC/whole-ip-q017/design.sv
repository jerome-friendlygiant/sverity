module top(input wire clk_a, input wire clk_b, input wire rst_n,
 input wire [3:0] data_i, input wire event_i, input wire mode_i,
 output wire [3:0] data_o, output wire event_o);
reg req_toggle; reg req_sync1; reg req_sync2; reg ack_toggle; reg ack_sync1; reg [3:0] data_o_r; reg event_o_r;
assign data_o=data_o_r; assign event_o=event_o_r;
always @(posedge clk_a or negedge rst_n) begin
 if (!rst_n) begin req_toggle<=0; ack_sync1<=0; end
 else begin ack_sync1<=ack_toggle; if (ack_sync1==req_toggle) req_toggle<=~req_toggle; end
end
always @(posedge clk_b or negedge rst_n) begin
 if (!rst_n) begin req_sync1<=0; req_sync2<=0; ack_toggle<=0; data_o_r<=0; event_o_r<=0; end
 else begin req_sync1<=req_toggle; req_sync2<=req_sync1; ack_toggle<=req_sync2; data_o_r<=0; event_o_r<=req_sync2 ^ ack_toggle; end
end
endmodule
