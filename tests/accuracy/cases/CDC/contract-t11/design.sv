module top_cdc_rfc1_t11(input logic src_clk, dst_clk, rst_n,
 input logic rd_en,
 output logic [2:0] wr_seen_rd
);
 logic [2:0] rd_bin;
 always_ff @(posedge src_clk or negedge rst_n) if (!rst_n) rd_bin<=0; else if (rd_en) rd_bin<=rd_bin+1'b1;
 always_ff @(posedge dst_clk or negedge rst_n) if (!rst_n) wr_seen_rd<=0; else wr_seen_rd<=rd_bin;
endmodule
