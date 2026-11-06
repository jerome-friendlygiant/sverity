// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic [3:0] src_i, output logic [7:0] out_o);
 logic [3:0] addr_q=0;
 logic [7:0] table_q [0:15];
 initial for(int k=0;k<16;k++) table_q[k]=8'(k*3);
 always_ff @(posedge sclk) addr_q<=src_i;
 always_ff @(posedge dclk) out_o<=table_q[addr_q];
endmodule
