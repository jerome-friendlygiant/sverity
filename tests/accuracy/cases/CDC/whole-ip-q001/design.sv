module top(input wire clk_a, input wire clk_b, input wire rst_n,
 input wire [3:0] data_i, input wire event_i, input wire mode_i,
 output wire [3:0] data_o, output wire event_o);
wire switched_clk = mode_i ? clk_a : clk_b;
reg [3:0] sink; reg [3:0] data_o_r; reg event_o_r;
assign data_o=data_o_r; assign event_o=event_o_r;
always @(posedge switched_clk or negedge rst_n) begin
 if (!rst_n) sink<=0; else sink<=sink + 1'b1;
end
always @(posedge clk_a or negedge rst_n) begin
 if (!rst_n) begin data_o_r<=0; event_o_r<=0; end else begin data_o_r<=0; event_o_r<=0; end
end
endmodule
