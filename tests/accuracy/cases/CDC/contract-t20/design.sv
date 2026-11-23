module top_cdc_rfc1_t20(input logic src_clk, dst_clk, rst_n,
 input logic event_in,
 output logic event_seen
);
   logic e1,e2;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin e1<=0;e2<=0;event_seen<=0;end else begin e1<=event_in;e2<=e1;event_seen<=event_seen|e2;end
endmodule
