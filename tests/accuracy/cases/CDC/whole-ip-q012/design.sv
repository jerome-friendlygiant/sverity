module top(input wire clk_a, input wire clk_b, input wire rst_n,
 input wire [3:0] data_i, input wire event_i, input wire mode_i,
 output wire [3:0] data_o, output wire event_o);
reg a_src; reg b_src; reg a1; reg a2; reg b1; reg b2; reg [3:0] data_o_r; reg event_o_r;
assign data_o=data_o_r; assign event_o=event_o_r;
always @(posedge clk_a or negedge rst_n) if (!rst_n) begin a_src<=0; b_src<=0; end else begin a_src<=data_i[0]; b_src<=data_i[1]; end
always @(posedge clk_b or negedge rst_n) begin
 if (!rst_n) begin a1<=0; a2<=0; b1<=0; b2<=0; data_o_r<=0; event_o_r<=0; end
 else begin a1<=a_src; a2<=a1; b1<=b_src; b2<=b1; data_o_r<={2'b0,a2,b2}; event_o_r<=0; end
end
endmodule
