module top_cdc_rfc1_t16(input logic src_clk, dst_clk, rst_n,
 input logic src_flag,
 output logic dst_flag
);
   (* ASYNC_REG="TRUE" *) logic s1,s2;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin s1<=0;s2<=0;end else begin s1<=src_flag;s2<=s1;end
 assign dst_flag=s2;
endmodule
