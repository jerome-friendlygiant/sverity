// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk,input logic src_i,output logic [7:0] out_o);
 logic run_q=0,m1=0,m2=0;
 initial out_o=0;
 always @(posedge sclk) run_q<=run_q|src_i;
 always @(posedge dclk) begin
  m1<=run_q;m2<=m1;
  if(m2) out_o<=out_o+1'b1;
 end
endmodule
