// Authored for the SVerity accuracy set.
(* blackbox *) module isolation_macro(input logic d,clamp,output wire q); endmodule
module top(input logic sclk,dclk,input logic [1:0] src_i,output logic out_o);
 logic sticky_q=0,m1=0,m2=0;
 wire isolated;
 always_ff @(posedge sclk) sticky_q<=sticky_q|src_i[0];
 isolation_macro iso(.d(sticky_q),.clamp(src_i[1]),.q(isolated));
 always_ff @(posedge dclk) begin m1<=isolated;m2<=m1;out_o<=m2;end
endmodule
