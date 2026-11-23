module case_ci03(input logic src_clk, input logic dst_clk, input logic rst_n, input logic req, output logic seen);
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) seen<=0; else seen<=req;
endmodule
