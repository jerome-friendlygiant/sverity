// Authored for the SVerity accuracy set.
// A board mode strap selects the destination's operating mode.
module top(input logic sclk,dclk,input logic src_i,output logic out_o);
 logic mode1=0,mode2=0;
 always_ff @(posedge dclk) begin
  mode1<=src_i;mode2<=mode1;
  out_o<=mode2;
 end
endmodule
