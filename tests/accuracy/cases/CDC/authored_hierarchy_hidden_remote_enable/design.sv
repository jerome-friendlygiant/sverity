// Authored for the SVerity accuracy set.
module sample_bank(input logic clk,en,output logic q);
 always_ff @(posedge clk) if(en) q<=~q;
endmodule
module top(input logic sclk,dclk, input logic src_i, output logic out_o);
 logic enable_q=0;
 always_ff @(posedge sclk) enable_q<=src_i;
 sample_bank bank(.clk(dclk),.en(enable_q),.q(out_o));
endmodule
