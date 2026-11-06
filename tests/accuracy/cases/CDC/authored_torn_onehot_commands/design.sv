// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic [1:0] src_i, output logic [3:0] out_o);
 logic [3:0] cmd_q=1,meta_q=1;
 always_ff @(posedge sclk) cmd_q<=4'b0001<<src_i;
 always_ff @(posedge dclk) begin
  meta_q<=cmd_q;
  out_o<=meta_q;
 end
endmodule
