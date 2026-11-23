module top(input wire clk_a, input wire clk_b, input wire rst_n,
 input wire [3:0] data_i, input wire event_i, input wire mode_i,
 output wire [3:0] data_o, output wire event_o);
reg [3:0] local_a; reg [3:0] local_b; reg [3:0] data_o_r; reg event_o_r;
assign data_o=data_o_r; assign event_o=event_o_r;
always @(posedge clk_a or negedge rst_n) begin
 if (!rst_n) local_a<=4'b0; else local_a<=local_a + 4'd7;
end
always @(posedge clk_b or negedge rst_n) begin
 if (!rst_n) begin local_b<=0; data_o_r<=0; event_o_r<=0; end
 else begin local_b<=local_b ^ 4'hC; data_o_r<=local_b; event_o_r<=local_b[0]; end
end
endmodule
