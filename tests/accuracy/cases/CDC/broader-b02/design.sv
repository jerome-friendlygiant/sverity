module case_ci02(input logic clk, input logic rst_n, input logic [3:0] d, output logic [3:0] q);
 logic [2:0] div; always_ff @(posedge clk or negedge rst_n) if(!rst_n) begin div<=0; q<=0; end else begin div<=div+1'b1; if(div==3'd7) q<=d; end
endmodule
