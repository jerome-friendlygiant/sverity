module top(input wire clk_a, input wire clk_b, input wire rst_n,
 input wire [3:0] data_i, input wire event_i, input wire mode_i,
 output wire [3:0] data_o, output wire event_o);
reg [3:0] src_a; reg [3:0] capture_b; reg [3:0] data_o_r; reg event_o_r;
assign data_o = data_o_r; assign event_o = event_o_r;
always @(posedge clk_a or negedge rst_n) begin
 if (!rst_n) src_a <= 4'b0; else src_a <= src_a + {3'b0,data_i[0]};
end
always @(posedge clk_b or negedge rst_n) begin
 if (!rst_n) begin capture_b <= 4'b0; data_o_r <= 4'b0; event_o_r <= 1'b0; end
 else begin capture_b <= src_a; data_o_r <= capture_b; event_o_r <= 1'b0; end
end
endmodule
