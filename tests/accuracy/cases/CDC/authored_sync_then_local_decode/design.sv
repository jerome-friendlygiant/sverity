// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk,input logic src_i,output logic [3:0] out_o);
 logic alarm_q=0,m1=0,m2=0;
 always_ff @(posedge sclk) alarm_q<=alarm_q|src_i;
 always_ff @(posedge dclk) begin
  m1<=alarm_q;m2<=m1;
  out_o<=m2 ? 4'h9 : 4'h2;
 end
endmodule
