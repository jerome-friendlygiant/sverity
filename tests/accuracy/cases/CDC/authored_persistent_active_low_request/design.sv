// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk,input logic src_i,output logic out_o);
 logic request_n=1,m1=1,m2=1;
 initial out_o=0;
 always @(posedge sclk) request_n<=request_n & ~src_i;
 always @(posedge dclk) begin
  m1<=request_n;m2<=m1;
  out_o<=~m2;
 end
endmodule
