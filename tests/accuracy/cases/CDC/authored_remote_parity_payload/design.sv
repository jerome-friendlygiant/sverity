// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic [6:0] src_i, output logic [7:0] out_o);
 logic [7:0] word_q=0,meta_q=0;
 always_ff @(posedge sclk) word_q<={^src_i,src_i};
 always_ff @(posedge dclk) begin
  meta_q<=word_q;
  out_o<=meta_q;
 end
endmodule
