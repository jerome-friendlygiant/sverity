// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic src_i, output logic out_o);
 logic request_q=0,meta_q=0;
 always_ff @(posedge sclk) request_q<=src_i;
 always_ff @(posedge dclk) begin
   meta_q<=request_q;
   out_o<=meta_q ? request_q : out_o;
 end
endmodule
