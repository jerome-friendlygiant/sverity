module top(input wire clk_a, input wire clk_b, input wire rst_n,
 input wire [3:0] data_i, input wire event_i, input wire mode_i,
 output wire [3:0] data_o, output wire event_o);
reg [2:0] wbin; reg [2:0] wgray; reg [2:0] rgray_s1; reg [2:0] rgray_s2; reg [2:0] data_o_r; reg event_o_r;
assign data_o=data_o_r; assign event_o=event_o_r;
always @(posedge clk_a or negedge rst_n) if (!rst_n) begin wbin<=0; wgray<=0; end else begin wbin<=wbin+3'd1; wgray<=(wbin+3'd1)^((wbin+3'd1)>>1); end
always @(posedge clk_b or negedge rst_n) begin
 if (!rst_n) begin rgray_s1<=0; rgray_s2<=0; data_o_r<=0; event_o_r<=0; end
 else begin rgray_s1<=wgray; rgray_s2<={rgray_s1[2:1],1'b0}; data_o_r<={1'b0,rgray_s2[1:0]}; event_o_r<=0; end
end
endmodule
