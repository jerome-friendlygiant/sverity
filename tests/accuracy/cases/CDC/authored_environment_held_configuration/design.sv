// Authored for the SVerity accuracy set.
// The board supplies a configuration word and a configuration-valid level.
module top(input logic sclk,dclk,input logic [8:0] src_i,output logic [7:0] out_o);
 logic v1=0,v2=0;
 always_ff @(posedge dclk) begin
  v1<=src_i[8];v2<=v1;
  if(v2) out_o<=src_i[7:0];
 end
endmodule
