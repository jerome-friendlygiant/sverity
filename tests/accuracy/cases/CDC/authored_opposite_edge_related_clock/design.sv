// Authored for the SVerity accuracy set.
module top(input logic sclk,input logic [7:0] src_i,output logic [7:0] out_o);
 logic [7:0] source_q=0;
 always_ff @(negedge sclk) source_q<=src_i;
 always_ff @(posedge sclk) out_o<=source_q;
endmodule
