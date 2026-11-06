// Authored for the SVerity accuracy set.
// sclk is the upstream reference; dclk is its board-forwarded capture clock.
module top(input logic sclk,dclk,input logic [7:0] src_i,output logic [7:0] out_o);
 always_ff @(posedge dclk) out_o<=src_i;
endmodule
