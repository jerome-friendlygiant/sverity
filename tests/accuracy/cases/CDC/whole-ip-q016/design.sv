module top(input wire clk_a, input wire clk_b, input wire rst_n,
 input wire [3:0] data_i, input wire event_i, input wire mode_i,
 output wire [3:0] data_o, output wire event_o);
reg p1; reg p2; reg [3:0] data_o_r; reg event_o_r;
assign data_o=data_o_r; assign event_o=event_o_r;
always @(posedge clk_b or negedge rst_n) begin
 if (!rst_n) begin p1<=0; p2<=0; data_o_r<=0; event_o_r<=0; end
 else begin p1<=event_i; p2<=p1; event_o_r<=p2; data_o_r<=4'b0; end
end
endmodule
