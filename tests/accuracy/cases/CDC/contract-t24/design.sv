module top_cdc_rfc1_t24(input logic src_clk, dst_clk, rst_n,
 output logic local_rst_n, dst_state
);
  logic r1,r2;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin r1<=0;r2<=0;end else begin r1<=1;r2<=r1;end
 assign local_rst_n=r2;
 always_ff @(posedge dst_clk or negedge local_rst_n) if (!local_rst_n) dst_state<=0; else dst_state<=~dst_state;
endmodule
