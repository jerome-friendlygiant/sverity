module top_cdc_rfc1_t17(input logic src_clk, dst_clk, rst_n,
 input logic src_flag,
 output logic dst_flag
);
   logic s1,s2,p;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin s1<=0;s2<=0;p<=0;dst_flag<=0;end else begin s1<=src_flag;s2<=s1;p<=s2;dst_flag<=p;end
endmodule
