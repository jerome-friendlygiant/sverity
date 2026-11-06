// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic [1:0] src_i, output logic out_o);
 logic [1:0] flags_q=0;
 logic a1=0,a2=0,b1=0,b2=0;
 always_ff @(posedge sclk) flags_q<=src_i;
 always_ff @(posedge dclk) begin
  a1<=flags_q[0]; a2<=a1;
  b1<=flags_q[1]; b2<=b1;
  out_o<=flags_q[0] ? b2 : a2;
 end
endmodule
