module top_cdc_rfc1_t27(input logic src_clk, dst_clk, rst_n,
 input logic level_in,
 output logic level_seen
);
   logic l1,l2;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin l1<=0;l2<=0;level_seen<=0;end else begin l1<=level_in;l2<=l1;level_seen<=l2;end
endmodule
