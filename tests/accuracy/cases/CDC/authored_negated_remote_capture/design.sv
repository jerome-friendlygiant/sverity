// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic src_i, output logic out_o);
 logic remote_n=1;
 always_ff @(posedge sclk) remote_n<=~src_i;
 always_ff @(posedge dclk) out_o<=~remote_n;
endmodule
