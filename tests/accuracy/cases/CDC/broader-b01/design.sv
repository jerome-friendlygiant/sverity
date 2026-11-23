module case_ib02(input logic clk, input logic rst_n, input logic [3:0] din, output logic [3:0] dout);
 logic [3:0] q; always_ff @(posedge clk or negedge rst_n) if(!rst_n) begin q<=0;dout<=0;end else begin q<=din;dout<=q;end
endmodule
