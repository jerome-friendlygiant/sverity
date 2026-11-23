module top_cdc_rfc1_t04(input logic src_clk, dst_clk, rst_n,
 input logic src_a,
 input logic src_b,
 output logic decision
);
   logic a1,a2,b1,b2;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin a1<=0;a2<=0;b1<=0;b2<=0;end else begin a1<=src_a;a2<=a1;b1<=src_b;b2<=b1;end
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) decision<=0; else decision<=a2 & ~b2;
endmodule
