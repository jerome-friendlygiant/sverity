module top_cdc_rfc1_t02(input logic src_clk, dst_clk, rst_n,
 input logic src_flag,
 output logic dst_flag
);
   logic sync1,sync2; logic early_seen;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin sync1<=0;sync2<=0;dst_flag<=0;early_seen<=0;end else begin sync1<=src_flag;sync2<=sync1;dst_flag<=sync2;early_seen<=sync1;end
endmodule
