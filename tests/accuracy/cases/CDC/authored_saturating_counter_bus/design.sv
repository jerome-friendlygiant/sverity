// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic src_i, output logic [7:0] out_o);
 logic [7:0] count_q=0,meta_q=0;
 always_ff @(posedge sclk)
  if(src_i && count_q!=8'hff) count_q<=count_q+1'b1;
 always_ff @(posedge dclk) begin
  meta_q<=count_q;
  out_o<=meta_q;
 end
endmodule
