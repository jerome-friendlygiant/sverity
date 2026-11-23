module top_cdc_rfc1_t03(input logic src_clk, dst_clk, rst_n,
 input logic [1:0] src_cmd,
 output logic [1:0] dst_cmd
);
   logic [1:0] s1,s2;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin s1<=0;s2<=0;dst_cmd<=0;end else begin s1<=src_cmd;s2<=s1;dst_cmd<=s2;end
endmodule
