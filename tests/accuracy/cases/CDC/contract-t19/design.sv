module top_cdc_rfc1_t19(input logic src_clk, dst_clk, rst_n,
 input logic src_a,
 input logic dst_mode,
 output logic both
);
   logic a1,a2;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin a1<=0;a2<=0;end else begin a1<=src_a;a2<=a1;end
 assign both=a2 & dst_mode;
endmodule
