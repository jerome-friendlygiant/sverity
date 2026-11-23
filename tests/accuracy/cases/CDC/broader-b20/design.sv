module case_ci04(input logic ref_clk, input logic mode, input logic rst_n, input logic d, output logic q);
 wire clk_sel = mode ? ref_clk : 1'b0;
 always_ff @(posedge clk_sel or negedge rst_n) if(!rst_n) q<=0; else q<=d;
endmodule
