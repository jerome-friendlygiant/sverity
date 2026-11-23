module case_ss01(input logic clk, input logic rst_n, input logic din, output logic dout);
 (* ASYNC_REG="TRUE" *) logic s1,s2; always_ff @(posedge clk or negedge rst_n) if(!rst_n) begin s1<=0;s2<=0;dout<=0;end else begin s1<=din;s2<=s1;dout<=s2;end
endmodule
