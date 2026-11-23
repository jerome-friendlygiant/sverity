module top_cdc_rfc1_t25(input logic src_clk, dst_clk, rst_n,
 input logic clk_a,
 input logic clk_b,
 input logic mode_a,
 output logic clk_out
);
   assign clk_out=mode_a?clk_a:clk_b;
endmodule
