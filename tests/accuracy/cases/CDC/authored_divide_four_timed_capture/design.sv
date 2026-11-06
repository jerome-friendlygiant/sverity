// Authored for the SVerity accuracy set.
module top(input logic sclk,input logic [7:0] src_i,output logic [7:0] out_o);
 logic [1:0] div_q=0;
 logic [7:0] source_q=0;
 always_ff @(posedge sclk) begin div_q<=div_q+1'b1;source_q<=src_i;end
 always_ff @(posedge div_q[1]) out_o<=source_q;
endmodule
