// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk,input logic [3:0] src_i,output logic out_o);
 logic sticky_q=0,m1=0,m2=0;
 always_ff @(posedge sclk) sticky_q<=sticky_q | (|src_i);
 always_ff @(posedge dclk) begin
   m1<=sticky_q;m2<=m1;
   out_o<=m2;
 end
endmodule
