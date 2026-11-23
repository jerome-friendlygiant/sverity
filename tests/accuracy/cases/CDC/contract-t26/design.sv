module top_cdc_rfc1_t26(input logic src_clk, dst_clk, rst_n,
 input logic src_data,
 output logic dst_data
);
   logic d1,d2;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin d1<=0;d2<=0;end else begin d1<=src_data;d2<=d1;end
 assign dst_data=d2;
endmodule
