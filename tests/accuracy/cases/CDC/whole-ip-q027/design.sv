module top(input wire clk_a, input wire clk_b, input wire rst_n,
 input wire [3:0] data_i, input wire event_i, input wire mode_i,
 output wire [3:0] data_o, output wire event_o);
reg src_a; reg s1; reg s2; reg [3:0] data_o_r; reg event_o_r;
assign data_o=data_o_r; assign event_o=event_o_r;
always @(posedge clk_a or negedge rst_n) if (!rst_n) src_a<=0; else src_a<=data_i[0];
always @(posedge clk_b or negedge mode_i) if (!mode_i) begin s1<=0; s2<=0; end else begin s1<=src_a; s2<=s1; end
always @(posedge clk_b or negedge rst_n) if (!rst_n) begin data_o_r<=0; event_o_r<=0; end else begin data_o_r<={3'b0,s2}; event_o_r<=0; end
endmodule
