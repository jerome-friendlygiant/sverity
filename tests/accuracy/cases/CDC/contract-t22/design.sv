module top_cdc_rfc1_t22(input logic src_clk, dst_clk, rst_n,
 input logic advance,
 output logic [2:0] ptr_sync
);
   logic [2:0] bin,gray,g1;
 always_ff @(posedge src_clk or negedge rst_n) if(!rst_n) begin bin<=0;gray<=0;end else if(advance) begin bin<=bin+1'b1;gray<=((bin+1'b1)>>1)^(bin+1'b1);end
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin g1<=0;ptr_sync<=0;end else begin g1<=gray;ptr_sync<=g1;end
endmodule
