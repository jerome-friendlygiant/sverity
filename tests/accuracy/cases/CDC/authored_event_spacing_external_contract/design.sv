// Authored for the SVerity accuracy set.
// Events arrive from an external controller; no minimum separation is specified.
module top(input logic sclk,dclk,input logic src_i,output logic out_o);
 logic event_q=0,m1=0,m2=0,seen_q=0;
 always_ff @(posedge sclk) if(src_i) event_q<=~event_q;
 always_ff @(posedge dclk) begin
  m1<=event_q;m2<=m1;seen_q<=m2;
  out_o<=m2^seen_q;
 end
endmodule
