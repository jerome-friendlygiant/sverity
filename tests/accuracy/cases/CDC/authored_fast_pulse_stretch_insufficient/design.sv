// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic src_i, output logic out_o);
 logic [1:0] stretch_q=0;
 logic meta_q=0;
 always_ff @(posedge sclk) stretch_q<={stretch_q[0],src_i};
 always_ff @(posedge dclk) begin
  meta_q<=|stretch_q;
  out_o<=meta_q;
 end
endmodule
