module case_cr02(input logic src_clk, input logic dst_clk, input logic src_rst_n, input logic dst_rst_n, input logic d, output logic q);
 logic a,b; always_ff @(posedge src_clk or negedge src_rst_n) if(!src_rst_n) a<=0; else a<=d;
 always_ff @(posedge dst_clk or negedge dst_rst_n) if(!dst_rst_n) begin b<=0;q<=0;end else begin b<=a;q<=b;end
endmodule
