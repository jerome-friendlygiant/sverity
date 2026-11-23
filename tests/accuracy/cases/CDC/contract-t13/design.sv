module top_cdc_rfc1_t13(input logic src_clk, dst_clk, rst_n,
 output logic dst_state
);
  always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) dst_state<=0; else dst_state<=1;
endmodule
