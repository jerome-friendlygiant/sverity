module top_cdc_rfc1_t12(input logic src_clk, dst_clk, rst_n,
 input logic rd_en,
 output logic [2:0] wr_seen_rd
);
   logic [2:0] rd_bin,rd_gray,g1;
 always_ff @(posedge src_clk or negedge rst_n) if(!rst_n) begin rd_bin<=0;rd_gray<=0;end else if(rd_en) begin rd_bin<=rd_bin+1'b1;rd_gray<=((rd_bin+1'b1)>>1)^(rd_bin+1'b1);end
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin g1<=0;wr_seen_rd<=0;end else begin g1<=rd_gray;wr_seen_rd[2:1]<=g1[2:1];wr_seen_rd[0]<=rd_gray[0];end
endmodule
