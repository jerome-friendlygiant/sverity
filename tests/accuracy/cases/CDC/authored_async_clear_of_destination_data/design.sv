// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic src_i, output logic [3:0] out_o);
 logic clear_q=0;
 always_ff @(posedge sclk) clear_q<=src_i;
 always_ff @(posedge dclk) begin
   if(clear_q) out_o<=0;
   else out_o<=out_o+1'b1;
 end
endmodule
